#ifdef PORTABLE
// Bottom-screen companion bridge. See include/platform/dualscreen.h.
//
// Threads: everything that reads game state runs on the game thread, from
// DualScreen_FrameHook. The only things another thread touches are the
// published JSON text, the injected key queue and the pending tap request,
// and all three are guarded by sLock.

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "battle.h"
#include "battle_main.h"
#include "data.h"
#include "event_data.h"
#include "item.h"
#include "money.h"
#include "move.h"
#include "overworld.h"
#include "pokemon.h"
#include "region_map.h"
#include "platform/dualscreen.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"

#define DS_JSON_CAPACITY 16384
#define DS_PUBLISH_INTERVAL 4   // frames between snapshots
#define DS_KEY_QUEUE_SIZE 16
#define DS_TAP_TIMEOUT 180      // frames a tap may take before it is dropped

struct InjectedKeys
{
    u16 keys;
    u16 frames;
};

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;

// Guarded by sLock.
static char sPublishedJson[DS_JSON_CAPACITY] = "{\"v\":1,\"frame\":0,\"inGame\":false,\"overworld\":false,\"party\":[],\"battle\":null}";
static struct InjectedKeys sKeyQueue[DS_KEY_QUEUE_SIZE];
static u32 sKeyHead;
static u32 sKeyCount;
static u32 sRequestKind;
static u32 sRequestIndex;

// Game thread only.
static struct DualScreenSnapshot sSnapshot;
static char sWorkJson[DS_JSON_CAPACITY];
static u32 sFrame;
static bool8 sSeenOverworld;
static u32 sTapKind;
static u32 sTapIndex;
static u32 sTapFramesLeft;

// ---------------------------------------------------------------------------
// Game text to UTF-8 (the single-byte Latin table of charmap.txt)
// ---------------------------------------------------------------------------

static const struct { u8 code; const char *text; } sCharTable[] =
{
    {0x00, " "}, {0x01, "À"}, {0x02, "Á"}, {0x03, "Â"}, {0x04, "Ç"}, {0x05, "È"}, {0x06, "É"}, {0x07, "Ê"},
    {0x08, "Ë"}, {0x09, "Ì"}, {0x0B, "Î"}, {0x0C, "Ï"}, {0x0D, "Ò"}, {0x0E, "Ó"}, {0x0F, "Ô"}, {0x10, "Œ"},
    {0x11, "Ù"}, {0x12, "Ú"}, {0x13, "Û"}, {0x14, "Ñ"}, {0x15, "ß"}, {0x16, "à"}, {0x17, "á"}, {0x19, "ç"},
    {0x1A, "è"}, {0x1B, "é"}, {0x1C, "ê"}, {0x1D, "ë"}, {0x1E, "ì"}, {0x20, "î"}, {0x21, "ï"}, {0x22, "ò"},
    {0x23, "ó"}, {0x24, "ô"}, {0x25, "œ"}, {0x26, "ù"}, {0x27, "ú"}, {0x28, "û"}, {0x29, "ñ"}, {0x2A, "º"},
    {0x2B, "ª"}, {0x2D, "&"}, {0x2E, "+"}, {0x34, "Lv"}, {0x35, "="}, {0x36, ";"}, {0x39, " "}, {0x51, "¿"},
    {0x52, "¡"}, {0x53, "PK"}, {0x54, "MN"}, {0x55, "PO"}, {0x56, "Ké"}, {0x5A, "Í"}, {0x5B, "%"}, {0x5C, "("},
    {0x5D, ")"}, {0x68, "â"}, {0x6F, "í"}, {0x77, " "}, {0x79, "↑"}, {0x7A, "↓"}, {0x7B, "←"}, {0x7C, "→"},
    {0x85, "<"}, {0x86, ">"}, {0xAB, "!"}, {0xAC, "?"}, {0xAD, "."}, {0xAE, "-"}, {0xAF, "·"}, {0xB0, "…"},
    {0xB1, "“"}, {0xB2, "”"}, {0xB3, "‘"}, {0xB4, "'"}, {0xB5, "♂"}, {0xB6, "♀"}, {0xB7, "¥"}, {0xB8, ","},
    {0xB9, "×"}, {0xBA, "/"}, {0xEF, "▶"}, {0xF0, ":"}, {0xF1, "Ä"}, {0xF2, "Ö"}, {0xF3, "Ü"}, {0xF4, "ä"},
    {0xF5, "ö"}, {0xF6, "ü"},
};

// Returns NULL for a character with no text form, which is then skipped.
static const char *DecodeGameChar(u8 code, char *scratch)
{
    u32 i;

    scratch[1] = '\0';
    if (code >= CHAR_0 && code <= CHAR_0 + 9)
        scratch[0] = '0' + (code - CHAR_0);
    else if (code >= CHAR_A && code <= CHAR_A + 25)
        scratch[0] = 'A' + (code - CHAR_A);
    else if (code >= CHAR_a && code <= CHAR_a + 25)
        scratch[0] = 'a' + (code - CHAR_a);
    else
    {
        for (i = 0; i < ARRAY_COUNT(sCharTable); i++)
        {
            if (sCharTable[i].code == code)
                return sCharTable[i].text;
        }
        return NULL;
    }
    return scratch;
}

// Decodes at most maxLength game characters. Stops at the terminator and at
// control codes, which take arguments and never appear in the names read here.
static void DecodeGameString(char *dest, int capacity, const u8 *src, int maxLength)
{
    int length = 0;
    int i;

    for (i = 0; src != NULL && i < maxLength; i++)
    {
        char scratch[2];
        const char *text;
        int textLength;

        if (src[i] >= 0xF7) // icons, placeholders, control codes, line breaks, EOS
            break;
        text = DecodeGameChar(src[i], scratch);
        if (text == NULL)
            continue;
        textLength = strlen(text);
        if (length + textLength > capacity - 1)
            break;
        memcpy(dest + length, text, textLength);
        length += textLength;
    }
    // Names padded to a fixed width end in spaces.
    while (length > 0 && dest[length - 1] == ' ')
        length--;
    dest[length] = '\0';
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

static void StatusToText(char *dest, u32 status, u32 hp)
{
    const char *text = "";

    if (hp == 0)
        text = "FNT";
    else if (status & STATUS1_SLEEP)
        text = "SLP";
    else if (status & STATUS1_TOXIC_POISON)
        text = "TOX";
    else if (status & STATUS1_POISON)
        text = "PSN";
    else if (status & STATUS1_BURN)
        text = "BRN";
    else if (status & STATUS1_FREEZE)
        text = "FRZ";
    else if (status & STATUS1_FROSTBITE)
        text = "FRB";
    else if (status & STATUS1_PARALYSIS)
        text = "PAR";
    strcpy(dest, text);
}

static void FillItemName(char *dest, u32 item)
{
    dest[0] = '\0';
    if (item != ITEM_NONE && item < ITEMS_COUNT)
        DecodeGameString(dest, DS_TEXT_LENGTH, GetItemName(item), ITEM_NAME_LENGTH);
}

static bool32 FillMove(struct DualScreenMove *dest, u32 move, u32 pp, u32 maxPp)
{
    u32 type;

    if (move == MOVE_NONE || move >= MOVES_COUNT)
        return FALSE;
    type = GetMoveType(move);
    dest->id = move;
    dest->pp = pp;
    dest->maxPp = maxPp;
    dest->type = type;
    DecodeGameString(dest->name, sizeof(dest->name), GetMoveName(move), MOVE_NAME_LENGTH);
    dest->typeName[0] = '\0';
    if (type < NUMBER_OF_MON_TYPES)
        DecodeGameString(dest->typeName, sizeof(dest->typeName), gTypesInfo[type].name, TYPE_NAME_LENGTH);
    return TRUE;
}

static bool32 FillPartyMon(struct DualScreenMon *dest, struct Pokemon *mon)
{
    u8 nickname[POKEMON_NAME_BUFFER_SIZE];
    u32 species, ppBonuses, i;

    memset(dest, 0, sizeof(*dest));
    if (!GetMonData(mon, MON_DATA_SANITY_HAS_SPECIES))
        return FALSE;
    species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG);
    if (species == SPECIES_NONE)
        return FALSE;

    dest->species = species;
    dest->isEgg = (species == SPECIES_EGG);
    dest->level = GetMonData(mon, MON_DATA_LEVEL);
    dest->hp = GetMonData(mon, MON_DATA_HP);
    dest->maxHp = GetMonData(mon, MON_DATA_MAX_HP);
    DecodeGameString(dest->speciesName, sizeof(dest->speciesName), GetSpeciesName(species), POKEMON_NAME_LENGTH);
    GetMonData(mon, MON_DATA_NICKNAME, nickname);
    DecodeGameString(dest->nickname, sizeof(dest->nickname), nickname, POKEMON_NAME_LENGTH);
    if (dest->isEgg)
        return TRUE;

    StatusToText(dest->status, GetMonData(mon, MON_DATA_STATUS), dest->hp);
    FillItemName(dest->itemName, GetMonData(mon, MON_DATA_HELD_ITEM));
    ppBonuses = GetMonData(mon, MON_DATA_PP_BONUSES);
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u32 move = GetMonData(mon, MON_DATA_MOVE1 + i);

        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        if (FillMove(&dest->moves[dest->moveCount], move, GetMonData(mon, MON_DATA_PP1 + i),
                     CalculatePPWithBonus(move, ppBonuses, i)))
            dest->moveCount++;
    }
    return TRUE;
}

// withMoves is for the player's own mon. The moves keep their slots, since
// the bottom screen's buttons stand for the game's four cursor positions,
// and the game packs moves into the first slots.
static bool32 FillBattleMon(struct DualScreenMon *dest, const struct BattlePokemon *mon, bool32 withMoves)
{
    u32 i;

    memset(dest, 0, sizeof(*dest));
    if (mon->species == SPECIES_NONE || mon->species >= NUM_SPECIES)
        return FALSE;

    dest->species = mon->species;
    dest->level = mon->level;
    dest->hp = mon->hp;
    dest->maxHp = mon->maxHP;
    DecodeGameString(dest->speciesName, sizeof(dest->speciesName), GetSpeciesName(mon->species), POKEMON_NAME_LENGTH);
    DecodeGameString(dest->nickname, sizeof(dest->nickname), mon->nickname, POKEMON_NAME_LENGTH);
    StatusToText(dest->status, mon->status1, mon->hp);
    if (!withMoves)
        return TRUE;

    FillItemName(dest->itemName, mon->item);
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u32 move = mon->moves[i];

        if (move == MOVE_NONE || move >= MOVES_COUNT)
            break;
        if (FillMove(&dest->moves[dest->moveCount], move, mon->pp[i],
                     CalculatePPWithBonus(move, mon->ppBonuses, i)))
            dest->moveCount++;
    }
    return TRUE;
}

// The battle globals are only meaningful while these exist. Natively a read
// through a NULL gBattleStruct is a crash, not the zero the GBA returned.
static bool32 IsBattleRunning(void)
{
    return gMain.inBattle
        && gBattleStruct != NULL
        && gBattleResources != NULL
        && gBattlersCount > 0
        && gBattlersCount <= MAX_BATTLERS_COUNT;
}

static void FillBattle(struct DualScreenBattle *dest)
{
    u32 battler, foe;

    memset(dest, 0, sizeof(*dest));
    if (!IsBattleRunning())
        return;

    dest->active = TRUE;
    dest->isDouble = IsDoubleBattle() != 0;
    dest->isTrainer = (gBattleTypeFlags & BATTLE_TYPE_TRAINER) != 0;
    dest->menu = DualScreen_GetPlayerMenu(&battler);
    if (dest->menu == DS_MENU_NONE)
        battler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);

    if (battler < gBattlersCount)
    {
        dest->actionCursor = gActionSelectionCursor[battler] & 3;
        dest->moveCursor = gMoveSelectionCursor[battler] & 3;
        dest->partyIndex = gBattlerPartyIndexes[battler];
        dest->hasPlayerMon = FillBattleMon(&dest->playerMon, &gBattleMons[battler], TRUE);
    }

    foe = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
    if (dest->isDouble && foe < gBattlersCount && (gAbsentBattlerFlags & (1u << foe)))
        foe = GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT);
    if (foe < gBattlersCount && !(gAbsentBattlerFlags & (1u << foe)))
        dest->hasFoe = FillBattleMon(&dest->foe, &gBattleMons[foe], FALSE);
}

void DualScreen_FillSnapshot(struct DualScreenSnapshot *snapshot)
{
    u8 mapName[64];
    u32 i;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->frame = sFrame;

    // The save blocks and the party hold nothing useful until a game has been
    // continued or started, and the field is the first screen after either.
    if (gMain.callback2 == CB2_Overworld)
    {
        sSeenOverworld = TRUE;
        snapshot->overworld = TRUE;
    }
    if (!sSeenOverworld || gSaveBlock1Ptr == NULL || gSaveBlock2Ptr == NULL)
        return;
    snapshot->inGame = TRUE;

    DecodeGameString(snapshot->playerName, sizeof(snapshot->playerName), gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH);
    snapshot->money = GetMoney(&gSaveBlock1Ptr->money);
    for (i = 0; i < NUM_BADGES; i++)
    {
        if (FlagGet(FLAG_BADGE01_GET + i))
            snapshot->badges++;
    }
    snapshot->playHours = gSaveBlock2Ptr->playTimeHours;
    snapshot->playMinutes = gSaveBlock2Ptr->playTimeMinutes;
    snapshot->playSeconds = gSaveBlock2Ptr->playTimeSeconds;
    mapName[0] = EOS;
    GetMapName(mapName, gMapHeader.regionMapSectionId, 0);
    DecodeGameString(snapshot->mapName, sizeof(snapshot->mapName), mapName, sizeof(mapName) - 1);

    // gPlayerPartyCount is not read: it is stale while the party is edited.
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (!FillPartyMon(&snapshot->party[snapshot->partyCount], &gPlayerParty[i]))
            break;
        snapshot->partyCount++;
    }

    FillBattle(&snapshot->battle);
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

struct JsonWriter
{
    char *buffer;
    int length;
    int capacity;
    bool8 overflowed;
};

static void JsonPut(struct JsonWriter *writer, const char *format, ...)
{
    va_list args;
    int written;

    if (writer->overflowed)
        return;
    va_start(args, format);
    written = vsnprintf(writer->buffer + writer->length, writer->capacity - writer->length, format, args);
    va_end(args);
    if (written < 0 || written >= writer->capacity - writer->length)
        writer->overflowed = TRUE;
    else
        writer->length += written;
}

static void JsonPutString(struct JsonWriter *writer, const char *text)
{
    JsonPut(writer, "\"");
    for (; *text != '\0'; text++)
    {
        unsigned char c = *text;

        if (c == '"' || c == '\\')
            JsonPut(writer, "\\%c", c);
        else if (c < 0x20)
            JsonPut(writer, "\\u%04x", c);
        else
            JsonPut(writer, "%c", c);
    }
    JsonPut(writer, "\"");
}

static void JsonPutMoves(struct JsonWriter *writer, const struct DualScreenMon *mon)
{
    u32 i;

    JsonPut(writer, "\"moves\":[");
    for (i = 0; i < mon->moveCount && i < MAX_MON_MOVES; i++)
    {
        const struct DualScreenMove *move = &mon->moves[i];

        JsonPut(writer, "%s{\"id\":%u,\"name\":", i == 0 ? "" : ",", move->id);
        JsonPutString(writer, move->name);
        JsonPut(writer, ",\"pp\":%u,\"maxPp\":%u,\"typeId\":%u,\"type\":", move->pp, move->maxPp, move->type);
        JsonPutString(writer, move->typeName);
        JsonPut(writer, "}");
    }
    JsonPut(writer, "]");
}

static void JsonPutMon(struct JsonWriter *writer, const struct DualScreenMon *mon, bool32 withMoves)
{
    JsonPut(writer, "{\"speciesId\":%u,\"species\":", mon->species);
    JsonPutString(writer, mon->speciesName);
    JsonPut(writer, ",\"nick\":");
    JsonPutString(writer, mon->nickname);
    JsonPut(writer, ",\"egg\":%s,\"lv\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":",
            mon->isEgg ? "true" : "false", mon->level, mon->hp, mon->maxHp);
    JsonPutString(writer, mon->status);
    if (withMoves)
    {
        JsonPut(writer, ",\"item\":");
        JsonPutString(writer, mon->itemName);
        JsonPut(writer, ",");
        JsonPutMoves(writer, mon);
    }
    JsonPut(writer, "}");
}

int DualScreen_SnapshotToJson(const struct DualScreenSnapshot *snapshot, char *dest, int capacity)
{
    static const char *const sMenuNames[] = { "none", "action", "move", "target" };
    struct JsonWriter writer = { dest, 0, capacity, FALSE };
    u32 i;

    JsonPut(&writer, "{\"v\":1,\"frame\":%u,\"inGame\":%s,\"overworld\":%s",
            (unsigned)snapshot->frame, snapshot->inGame ? "true" : "false", snapshot->overworld ? "true" : "false");
    if (snapshot->inGame)
    {
        JsonPut(&writer, ",\"player\":{\"name\":");
        JsonPutString(&writer, snapshot->playerName);
        JsonPut(&writer, ",\"money\":%u,\"badges\":%u,\"hours\":%u,\"minutes\":%u,\"seconds\":%u,\"map\":",
                (unsigned)snapshot->money, snapshot->badges, snapshot->playHours, snapshot->playMinutes, snapshot->playSeconds);
        JsonPutString(&writer, snapshot->mapName);
        JsonPut(&writer, "}");
    }

    JsonPut(&writer, ",\"party\":[");
    for (i = 0; i < snapshot->partyCount && i < PARTY_SIZE; i++)
    {
        if (i != 0)
            JsonPut(&writer, ",");
        JsonPutMon(&writer, &snapshot->party[i], TRUE);
    }
    JsonPut(&writer, "]");

    if (snapshot->battle.active)
    {
        const struct DualScreenBattle *battle = &snapshot->battle;

        JsonPut(&writer, ",\"battle\":{\"menu\":\"%s\",\"double\":%s,\"trainer\":%s,\"actionCursor\":%u,\"moveCursor\":%u,\"partyIndex\":%u",
                sMenuNames[battle->menu & 3], battle->isDouble ? "true" : "false", battle->isTrainer ? "true" : "false",
                battle->actionCursor, battle->moveCursor, battle->partyIndex);
        JsonPut(&writer, ",\"mon\":");
        if (battle->hasPlayerMon)
            JsonPutMon(&writer, &battle->playerMon, TRUE);
        else
            JsonPut(&writer, "null");
        JsonPut(&writer, ",\"foe\":");
        if (battle->hasFoe)
            JsonPutMon(&writer, &battle->foe, FALSE);
        else
            JsonPut(&writer, "null");
        JsonPut(&writer, "}");
    }
    else
    {
        JsonPut(&writer, ",\"battle\":null");
    }
    JsonPut(&writer, "}");

    if (writer.overflowed)
    {
        // Never hand out cut-off JSON.
        writer.length = snprintf(dest, capacity, "{\"v\":1,\"frame\":%u,\"error\":\"overflow\"}", (unsigned)snapshot->frame);
        if (writer.length >= capacity)
            writer.length = 0;
        dest[writer.length] = '\0';
    }
    return writer.length;
}

static void PublishSnapshot(void)
{
    int length;

    DualScreen_FillSnapshot(&sSnapshot);
    length = DualScreen_SnapshotToJson(&sSnapshot, sWorkJson, sizeof(sWorkJson));
    pthread_mutex_lock(&sLock);
    memcpy(sPublishedJson, sWorkJson, length + 1);
    pthread_mutex_unlock(&sLock);
}

int DualScreen_CopyJson(char *dest, int capacity)
{
    int length;

    if (capacity <= 0)
        return 0;
    pthread_mutex_lock(&sLock);
    length = strlen(sPublishedJson);
    if (length > capacity - 1)
        length = 0; // too small for the whole text: give nothing rather than a piece
    memcpy(dest, sPublishedJson, length);
    pthread_mutex_unlock(&sLock);
    dest[length] = '\0';
    return length;
}

// ---------------------------------------------------------------------------
// Injected buttons
// ---------------------------------------------------------------------------

static void PushKeysLocked(u16 keys, u32 frames)
{
    if (sKeyCount < DS_KEY_QUEUE_SIZE)
    {
        struct InjectedKeys *entry = &sKeyQueue[(sKeyHead + sKeyCount) % DS_KEY_QUEUE_SIZE];

        entry->keys = keys;
        entry->frames = frames;
        sKeyCount++;
    }
}

// Holds the buttons for the given number of frames, then lets go for one
// frame so that the next press of the same button counts as a new press.
void DualScreen_InjectKeys(u16 keys, u32 frames)
{
    if (frames == 0)
        frames = 1;
    if (frames > 600)
        frames = 600;
    pthread_mutex_lock(&sLock);
    if (sKeyCount + 2 <= DS_KEY_QUEUE_SIZE)
    {
        PushKeysLocked(keys, frames);
        PushKeysLocked(0, 1);
    }
    pthread_mutex_unlock(&sLock);
}

u16 DualScreen_ConsumeInjectedKeys(void)
{
    u16 keys = 0;

    pthread_mutex_lock(&sLock);
    if (sKeyCount > 0)
    {
        struct InjectedKeys *entry = &sKeyQueue[sKeyHead];

        keys = entry->keys;
        if (--entry->frames == 0)
        {
            sKeyHead = (sKeyHead + 1) % DS_KEY_QUEUE_SIZE;
            sKeyCount--;
        }
    }
    pthread_mutex_unlock(&sLock);
    return keys;
}

void DualScreen_RequestTap(u32 kind, u32 index)
{
    if ((kind != DS_TAP_MOVE && kind != DS_TAP_ACTION) || index > 3)
        return;
    pthread_mutex_lock(&sLock);
    sRequestKind = kind;
    sRequestIndex = index;
    pthread_mutex_unlock(&sLock);
}

// The button that takes a 2x2 menu cursor one step toward the wanted slot,
// or 0 when it is there. Only moves onto one of the first slotCount slots,
// as the move menu does when a mon knows fewer than four moves.
static u16 StepCursor(u32 cursor, u32 wanted, u32 slotCount)
{
    if ((cursor ^ wanted) & 1)
    {
        if (cursor & 1)
            return DPAD_LEFT;
        if ((cursor ^ 1) < slotCount)
            return DPAD_RIGHT;
        return DPAD_UP; // the slot to the right is empty, so go around
    }
    if ((cursor ^ wanted) & 2)
    {
        if (cursor & 2)
            return DPAD_UP;
        if ((cursor ^ 2) < slotCount)
            return DPAD_DOWN;
        return DPAD_LEFT;
    }
    return 0;
}

// Turns a pending tap into the button presses a player would make, one
// press at a time, looking at where the game's own cursor is before each.
// A tap is therefore never applied to a menu other than the one on screen.
static void DriveTap(void)
{
    u32 battler, menu, cursor;
    u16 key = 0;
    bool32 queueBusy;
    bool32 isNew = FALSE;

    pthread_mutex_lock(&sLock);
    if (sRequestKind != DS_TAP_NONE)
    {
        sTapKind = sRequestKind;
        sTapIndex = sRequestIndex;
        sTapFramesLeft = DS_TAP_TIMEOUT;
        sRequestKind = DS_TAP_NONE;
        isNew = TRUE;
    }
    queueBusy = sKeyCount != 0;
    pthread_mutex_unlock(&sLock);

    if (sTapKind == DS_TAP_NONE)
        return;
    if (!IsBattleRunning() || --sTapFramesLeft == 0)
    {
        sTapKind = DS_TAP_NONE;
        return;
    }

    menu = DualScreen_GetPlayerMenu(&battler);
    // A tap made while no menu is open (during an attack, say) is dropped
    // rather than kept for the next turn.
    if (isNew && menu != DS_MENU_ACTION && menu != DS_MENU_MOVE)
    {
        sTapKind = DS_TAP_NONE;
        return;
    }
    if (queueBusy || battler >= gBattlersCount)
        return;

    if (menu == DS_MENU_ACTION)
    {
        u32 wanted = (sTapKind == DS_TAP_MOVE) ? DS_ACTION_FIGHT : sTapIndex;

        cursor = gActionSelectionCursor[battler] & 3;
        key = StepCursor(cursor, wanted, 4);
        if (key == 0)
        {
            key = A_BUTTON;
            if (sTapKind == DS_TAP_ACTION)
                sTapKind = DS_TAP_NONE; // done; a move tap goes on into the move menu
        }
    }
    else if (menu == DS_MENU_MOVE)
    {
        if (sTapKind == DS_TAP_ACTION)
        {
            if (sTapIndex == DS_ACTION_FIGHT)
                sTapKind = DS_TAP_NONE; // already there
            else
                key = B_BUTTON;         // back to the action menu first
        }
        else if (sTapIndex >= gNumberOfMovesToChoose)
        {
            sTapKind = DS_TAP_NONE;
        }
        else
        {
            cursor = gMoveSelectionCursor[battler] & 3;
            key = StepCursor(cursor, sTapIndex, gNumberOfMovesToChoose);
            if (key == 0)
            {
                key = A_BUTTON;
                sTapKind = DS_TAP_NONE;
            }
        }
    }

    if (key != 0)
        DualScreen_InjectKeys(key, 1);
}

// ---------------------------------------------------------------------------
// Frame hooks
// ---------------------------------------------------------------------------

void DualScreen_FrameHook(void)
{
    sFrame++;
    DriveTap();
    if (sFrame % DS_PUBLISH_INTERVAL == 0)
        PublishSnapshot();
}

// "2700:MOVE1,2900:RUN" in HNS_TAP: the tap made on a frame, if any.
static void HeadlessTap(const char *script, unsigned long frame)
{
    static const struct { const char *name; u8 kind; u8 index; } names[] = {
        {"MOVE1", DS_TAP_MOVE, 0}, {"MOVE2", DS_TAP_MOVE, 1}, {"MOVE3", DS_TAP_MOVE, 2}, {"MOVE4", DS_TAP_MOVE, 3},
        {"FIGHT", DS_TAP_ACTION, DS_ACTION_FIGHT}, {"BAG", DS_TAP_ACTION, DS_ACTION_BAG},
        {"POKEMON", DS_TAP_ACTION, DS_ACTION_POKEMON}, {"RUN", DS_TAP_ACTION, DS_ACTION_RUN},
    };

    while (script != NULL && *script != '\0')
    {
        char *end;
        unsigned long start = strtoul(script, &end, 10);
        size_t length;
        u32 i;

        if (*end != ':')
            break;
        script = end + 1;
        length = strcspn(script, ",");
        for (i = 0; start == frame && i < ARRAY_COUNT(names); i++)
        {
            if (strlen(names[i].name) == length && strncmp(names[i].name, script, length) == 0)
                DualScreen_RequestTap(names[i].kind, names[i].index);
        }
        script += length;
        if (*script == ',')
            script++;
    }
}

void DualScreen_HeadlessFrame(unsigned long frame)
{
    static bool8 sRead;
    static unsigned long sDumpEvery;
    static const char *sTaps;

    if (!sRead)
    {
        const char *text = getenv("HNS_STATE_DUMP");

        sDumpEvery = text != NULL ? strtoul(text, NULL, 10) : 0;
        sTaps = getenv("HNS_TAP");
        sRead = TRUE;
    }
    HeadlessTap(sTaps, frame);
    DualScreen_FrameHook();
    if (sDumpEvery != 0 && frame % sDumpEvery == 0)
    {
        PublishSnapshot();
        fprintf(stderr, "state %lu %s\n", frame, sPublishedJson);
    }
}

// ---------------------------------------------------------------------------
// Android
// ---------------------------------------------------------------------------

#ifdef __ANDROID__
#include <jni.h>

JNIEXPORT jstring JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeGetStateJson(JNIEnv *env, jclass clazz)
{
    static char sJniJson[DS_JSON_CAPACITY]; // only the UI thread calls this

    (void)clazz;
    DualScreen_CopyJson(sJniJson, sizeof(sJniJson));
    return (*env)->NewStringUTF(env, sJniJson);
}

JNIEXPORT void JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeTap(JNIEnv *env, jclass clazz, jint kind, jint index)
{
    (void)env;
    (void)clazz;
    if (kind >= 0 && index >= 0)
        DualScreen_RequestTap(kind, index);
}

JNIEXPORT void JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeInjectKeys(JNIEnv *env, jclass clazz, jint keys, jint frames)
{
    (void)env;
    (void)clazz;
    if (frames > 0)
        DualScreen_InjectKeys(keys & 0x3FF, frames);
}
#endif // __ANDROID__

#endif // PORTABLE
