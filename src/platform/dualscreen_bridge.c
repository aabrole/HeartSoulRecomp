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
#include "decompress.h"
#include "graphics.h"
#include "item.h"
#include "item_icon.h"
#include "money.h"
#include "move.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "region_map.h"
#include "strings.h"
#include "platform/dualscreen.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"

#define DS_JSON_CAPACITY 32768
#define DS_PUBLISH_INTERVAL 4   // frames between snapshots
#define DS_BAG_JSON_CAPACITY (192 * 1024) // a full bag with every description is about 66K
#define DS_BAG_INTERVAL 30      // frames between bag snapshots while the bag is wanted
#define DS_ITEM_ICON_SIZE 24
#define DS_ITEM_ICON_CACHE 128  // decoded item icons kept
#define DS_ITEM_ICON_QUEUE 16
#define DS_ITEM_ICONS_PER_FRAME 4
#define DS_MON_ICON_PALETTES 6  // gMonIconPalettes in src/graphics.c
#define DS_KEY_QUEUE_SIZE 16
#define DS_TAP_TIMEOUT 180      // frames a tap may take before it is dropped

// src/platform/sdl2.c: above 1 while the game runs fast.
extern double timeScale;

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
static char sPublishedBagJson[DS_BAG_JSON_CAPACITY] = "{\"pockets\":[]}";
static bool8 sBagWanted;
static u16 sItemIconQueue[DS_ITEM_ICON_QUEUE];
static u32 sItemIconQueueCount;
static struct
{
    u16 item; // ITEM_NONE for a free entry
    u32 argb[DS_ITEM_ICON_SIZE * DS_ITEM_ICON_SIZE];
} sItemIcons[DS_ITEM_ICON_CACHE];
static u32 sItemIconNext; // the entry the next decoded icon replaces

// Game thread only.
static struct DualScreenSnapshot sSnapshot;
static char sWorkJson[DS_JSON_CAPACITY];
static char sWorkBagJson[DS_BAG_JSON_CAPACITY];
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

// Like DecodeGameString for running text such as item descriptions: a line
// break becomes a space, and the text may be longer.
static void DecodeGameText(char *dest, int capacity, const u8 *src, int maxLength)
{
    int length = 0;
    int i;

    for (i = 0; src != NULL && i < maxLength && src[i] != EOS; i++)
    {
        char scratch[2];
        const char *text;
        int textLength;

        if (src[i] == CHAR_NEWLINE || src[i] == CHAR_PROMPT_SCROLL || src[i] == CHAR_PROMPT_CLEAR)
            text = " ";
        else if (src[i] >= 0xF7)
            break; // placeholders and control codes with arguments
        else
            text = DecodeGameChar(src[i], scratch);
        if (text == NULL)
            continue;
        textLength = strlen(text);
        if (length + textLength > capacity - 1)
            break;
        memcpy(dest + length, text, textLength);
        length += textLength;
    }
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
    dest->category = GetMoveCategory(move);
    dest->power = min(GetMovePower(move), 255);
    dest->accuracy = min(GetMoveAccuracy(move), 255);
    DecodeGameString(dest->name, sizeof(dest->name), GetMoveName(move), MOVE_NAME_LENGTH);
    dest->typeName[0] = '\0';
    if (type < NUMBER_OF_MON_TYPES)
        DecodeGameString(dest->typeName, sizeof(dest->typeName), gTypesInfo[type].name, TYPE_NAME_LENGTH);
    return TRUE;
}

// The game's stat order (HP, ATK, DEF, SPEED, SPATK, SPDEF) to the order
// of the summary screen, which the bottom screen uses.
static u32 StatToDualScreen(u32 stat)
{
    switch (stat)
    {
    case STAT_HP:    return DS_STAT_HP;
    case STAT_ATK:   return DS_STAT_ATK;
    case STAT_DEF:   return DS_STAT_DEF;
    case STAT_SPATK: return DS_STAT_SPATK;
    case STAT_SPDEF: return DS_STAT_SPDEF;
    case STAT_SPEED: return DS_STAT_SPEED;
    default:         return DS_STAT_COUNT;
    }
}

static void FillPartyMonDetails(struct DualScreenMon *dest, struct Pokemon *mon, u32 species)
{
    static const u8 sStatData[DS_STAT_COUNT][3] =
    {
        [DS_STAT_HP]    = {MON_DATA_MAX_HP, MON_DATA_HP_IV,    MON_DATA_HP_EV},
        [DS_STAT_ATK]   = {MON_DATA_ATK,    MON_DATA_ATK_IV,   MON_DATA_ATK_EV},
        [DS_STAT_DEF]   = {MON_DATA_DEF,    MON_DATA_DEF_IV,   MON_DATA_DEF_EV},
        [DS_STAT_SPATK] = {MON_DATA_SPATK,  MON_DATA_SPATK_IV, MON_DATA_SPATK_EV},
        [DS_STAT_SPDEF] = {MON_DATA_SPDEF,  MON_DATA_SPDEF_IV, MON_DATA_SPDEF_EV},
        [DS_STAT_SPEED] = {MON_DATA_SPEED,  MON_DATA_SPEED_IV, MON_DATA_SPEED_EV},
    };
    u32 i, nature, ability, growthRate, exp, level;
    enum Ability lastUsedAbility;

    if (species >= NUM_SPECIES)
        return;
    dest->hasDetails = TRUE;
    dest->isShiny = IsMonShiny(mon) != 0;
    dest->gender = GetMonGender(mon);
    dest->types[0] = GetSpeciesType(species, 0);
    dest->types[1] = GetSpeciesType(species, 1);
    if (dest->types[1] == dest->types[0])
        dest->types[1] = TYPE_NONE;
    dest->types[2] = TYPE_NONE;
    dest->friendship = GetMonData(mon, MON_DATA_FRIENDSHIP);

    // GetMonAbility also sets gLastUsedAbility, which battle scripts read
    // later (Skill Swap's popup). Reading must leave the game as it was.
    lastUsedAbility = gLastUsedAbility;
    ability = GetMonAbility(mon);
    gLastUsedAbility = lastUsedAbility;
    if (ability < ABILITIES_COUNT)
        DecodeGameString(dest->abilityName, sizeof(dest->abilityName), gAbilitiesInfo[ability].name, ABILITY_NAME_LENGTH);

    // The nature that counts for stats, which a mint can change.
    nature = GetMonData(mon, MON_DATA_HIDDEN_NATURE);
    dest->natureUp = DS_STAT_COUNT;
    dest->natureDown = DS_STAT_COUNT;
    if (nature < NUM_NATURES)
    {
        DecodeGameString(dest->natureName, sizeof(dest->natureName), gNaturesInfo[nature].name, 16);
        if (gNaturesInfo[nature].statUp != gNaturesInfo[nature].statDown)
        {
            dest->natureUp = StatToDualScreen(gNaturesInfo[nature].statUp);
            dest->natureDown = StatToDualScreen(gNaturesInfo[nature].statDown);
        }
    }

    for (i = 0; i < DS_STAT_COUNT; i++)
    {
        dest->stats[i] = GetMonData(mon, sStatData[i][0]);
        dest->ivs[i] = GetMonData(mon, sStatData[i][1]);
        dest->evs[i] = GetMonData(mon, sStatData[i][2]);
    }

    growthRate = gSpeciesInfo[species].growthRate;
    exp = GetMonData(mon, MON_DATA_EXP);
    level = dest->level;
    if (level >= 1 && level < MAX_LEVEL)
    {
        u32 base = gExperienceTables[growthRate][level];
        u32 next = gExperienceTables[growthRate][level + 1];

        dest->expNextLevel = next > base ? next - base : 0;
        dest->expThisLevel = exp > base ? min(exp - base, dest->expNextLevel) : 0;
    }
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
    dest->iconSpecies = GetMonData(mon, MON_DATA_SPECIES);
    dest->personality = GetMonData(mon, MON_DATA_PERSONALITY);
    dest->isEgg = (species == SPECIES_EGG);
    dest->level = GetMonData(mon, MON_DATA_LEVEL);
    dest->hp = GetMonData(mon, MON_DATA_HP);
    dest->maxHp = GetMonData(mon, MON_DATA_MAX_HP);
    DecodeGameString(dest->speciesName, sizeof(dest->speciesName), GetSpeciesName(species), POKEMON_NAME_LENGTH);
    GetMonData(mon, MON_DATA_NICKNAME, nickname);
    DecodeGameString(dest->nickname, sizeof(dest->nickname), nickname, POKEMON_NAME_LENGTH);
    if (dest->isEgg)
        return TRUE;

    FillPartyMonDetails(dest, mon, species);

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
    dest->iconSpecies = mon->species;
    dest->personality = mon->personality;
    dest->level = mon->level;
    dest->hp = mon->hp;
    dest->maxHp = mon->maxHP;
    // The battle's types, which moves like Soak and Burn Up change. A type
    // listed twice counts once.
    for (i = 0; i < 3; i++)
    {
        u32 type = mon->types[i];

        if (type >= NUMBER_OF_MON_TYPES || type == TYPE_MYSTERY
         || (i > 0 && type == mon->types[0]) || (i > 1 && type == mon->types[1]))
            type = TYPE_NONE;
        dest->types[i] = type;
    }
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

    if (dest->isTrainer)
    {
        u32 i;

        for (i = 0; i < PARTY_SIZE; i++)
        {
            u32 species = GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG);

            if (species == SPECIES_NONE || species == SPECIES_EGG)
                continue;
            dest->foeTeamCount++;
            if (GetMonData(&gEnemyParty[i], MON_DATA_HP) != 0)
                dest->foeTeamLeft++;
        }
    }
}

void DualScreen_FillSnapshot(struct DualScreenSnapshot *snapshot)
{
    u8 mapName[64];
    u32 i;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->frame = sFrame;
    snapshot->fastForward = timeScale > 1.0;

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
        JsonPut(writer, ",\"pp\":%u,\"maxPp\":%u,\"typeId\":%u,\"cat\":%u,\"power\":%u,\"acc\":%u,\"type\":",
                move->pp, move->maxPp, move->type, move->category, move->power, move->accuracy);
        JsonPutString(writer, move->typeName);
        JsonPut(writer, "}");
    }
    JsonPut(writer, "]");
}

static void JsonPutStats(struct JsonWriter *writer, const char *name, const void *values, bool32 wide)
{
    u32 i;

    JsonPut(writer, ",\"%s\":[", name);
    for (i = 0; i < DS_STAT_COUNT; i++)
        JsonPut(writer, "%s%u", i == 0 ? "" : ",", wide ? ((const u16 *)values)[i] : ((const u8 *)values)[i]);
    JsonPut(writer, "]");
}

static void JsonPutDetails(struct JsonWriter *writer, const struct DualScreenMon *mon)
{
    JsonPut(writer, ",\"shiny\":%s,\"gender\":%u,\"friendship\":%u,\"ability\":",
            mon->isShiny ? "true" : "false", mon->gender, mon->friendship);
    JsonPutString(writer, mon->abilityName);
    JsonPut(writer, ",\"nature\":");
    JsonPutString(writer, mon->natureName);
    JsonPut(writer, ",\"natureUp\":%d,\"natureDown\":%d",
            mon->natureUp < DS_STAT_COUNT ? mon->natureUp : -1,
            mon->natureDown < DS_STAT_COUNT ? mon->natureDown : -1);
    JsonPutStats(writer, "stats", mon->stats, TRUE);
    JsonPutStats(writer, "ivs", mon->ivs, FALSE);
    JsonPutStats(writer, "evs", mon->evs, FALSE);
    JsonPut(writer, ",\"exp\":%u,\"expNext\":%u", (unsigned)mon->expThisLevel, (unsigned)mon->expNextLevel);
}

static void JsonPutMon(struct JsonWriter *writer, const struct DualScreenMon *mon, bool32 withMoves)
{
    u32 i;
    bool32 first = TRUE;

    JsonPut(writer, "{\"speciesId\":%u,\"iconSpecies\":%u,\"personality\":%u,\"types\":[",
            mon->species, mon->iconSpecies, (unsigned)mon->personality);
    for (i = 0; i < 3; i++)
    {
        if (mon->types[i] == TYPE_NONE)
            continue;
        JsonPut(writer, "%s%u", first ? "" : ",", mon->types[i]);
        first = FALSE;
    }
    JsonPut(writer, "],\"species\":");
    JsonPutString(writer, mon->speciesName);
    JsonPut(writer, ",\"nick\":");
    JsonPutString(writer, mon->nickname);
    JsonPut(writer, ",\"egg\":%s,\"lv\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":",
            mon->isEgg ? "true" : "false", mon->level, mon->hp, mon->maxHp);
    JsonPutString(writer, mon->status);
    if (mon->hasDetails)
        JsonPutDetails(writer, mon);
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

    JsonPut(&writer, "{\"v\":1,\"frame\":%u,\"inGame\":%s,\"overworld\":%s,\"ff\":%s",
            (unsigned)snapshot->frame, snapshot->inGame ? "true" : "false", snapshot->overworld ? "true" : "false",
            snapshot->fastForward ? "true" : "false");
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
        if (battle->isTrainer)
            JsonPut(&writer, ",\"foeTeam\":%u,\"foeTeamLeft\":%u", battle->foeTeamCount, battle->foeTeamLeft);
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
// Bag
// ---------------------------------------------------------------------------

// The pockets in the bag screen's order, each with its items in bag order.
static int BagToJson(char *dest, int capacity)
{
    struct JsonWriter writer = { dest, 0, capacity, FALSE };
    char text[256];
    u32 pocketId, slot;

    JsonPut(&writer, "{\"frame\":%u,\"pockets\":[", (unsigned)sFrame);
    for (pocketId = 0; pocketId < POCKETS_COUNT; pocketId++)
    {
        struct BagPocket *pocket = &gBagPockets[pocketId];
        bool32 first = TRUE;

        DecodeGameString(text, sizeof(text), gPocketNamesStringsTable[pocketId], 20);
        JsonPut(&writer, "%s{\"id\":%u,\"name\":", pocketId == 0 ? "" : ",", pocketId);
        JsonPutString(&writer, text);
        JsonPut(&writer, ",\"items\":[");
        for (slot = 0; pocket->itemSlots != NULL && slot < pocket->capacity; slot++)
        {
            struct ItemSlot item = BagPocket_GetSlotData(pocket, slot);
            enum Move move;

            if (item.itemId == ITEM_NONE || item.itemId >= ITEMS_COUNT || item.quantity == 0)
                continue;
            JsonPut(&writer, "%s{\"id\":%u,\"qty\":%u,\"name\":", first ? "" : ",", item.itemId, item.quantity);
            first = FALSE;
            DecodeGameString(text, sizeof(text), GetItemName(item.itemId), ITEM_NAME_LENGTH);
            JsonPutString(&writer, text);
            move = pocketId == POCKET_TM_HM ? GetItemTMHMMoveId(item.itemId) : MOVE_NONE;
            if (move != MOVE_NONE && move < MOVES_COUNT)
            {
                u32 type = GetMoveType(move);

                DecodeGameString(text, sizeof(text), GetMoveName(move), MOVE_NAME_LENGTH);
                JsonPut(&writer, ",\"move\":");
                JsonPutString(&writer, text);
                JsonPut(&writer, ",\"typeId\":%u", type < NUMBER_OF_MON_TYPES ? type : TYPE_NONE);
            }
            DecodeGameText(text, sizeof(text), GetItemDescription(item.itemId), 200);
            JsonPut(&writer, ",\"desc\":");
            JsonPutString(&writer, text);
            JsonPut(&writer, "}");
        }
        JsonPut(&writer, "]}");
    }
    JsonPut(&writer, "]}");

    if (writer.overflowed)
    {
        writer.length = snprintf(dest, capacity, "{\"frame\":%u,\"error\":\"overflow\",\"pockets\":[]}", (unsigned)sFrame);
        if (writer.length >= capacity)
            writer.length = 0;
        dest[writer.length] = '\0';
    }
    return writer.length;
}

static void PublishBag(void)
{
    int length;

    if (!sSeenOverworld || gSaveBlock2Ptr == NULL)
        return;
    length = BagToJson(sWorkBagJson, sizeof(sWorkBagJson));
    pthread_mutex_lock(&sLock);
    memcpy(sPublishedBagJson, sWorkBagJson, length + 1);
    pthread_mutex_unlock(&sLock);
}

void DualScreen_SetBagWanted(bool32 wanted)
{
    pthread_mutex_lock(&sLock);
    sBagWanted = wanted != 0;
    pthread_mutex_unlock(&sLock);
}

int DualScreen_CopyBagJson(char *dest, int capacity)
{
    int length;

    if (capacity <= 0)
        return 0;
    pthread_mutex_lock(&sLock);
    length = strlen(sPublishedBagJson);
    if (length > capacity - 1)
        length = 0;
    memcpy(dest, sPublishedBagJson, length);
    pthread_mutex_unlock(&sLock);
    dest[length] = '\0';
    return length;
}

// ---------------------------------------------------------------------------
// Type chart
// ---------------------------------------------------------------------------

// {"names":[...],"chart":[[...],...]} with chart[attack][defence] in
// 1/4096ths, as the game keeps it (4096 is neutral). TYPE_NONE and the
// unused ??? type are in it too, so type ids index it directly.
int DualScreen_TypeChartJson(char *dest, int capacity)
{
    struct JsonWriter writer = { dest, 0, capacity, FALSE };
    char name[DS_TEXT_LENGTH];
    u32 attack, defence;

    JsonPut(&writer, "{\"names\":[");
    for (attack = 0; attack < NUMBER_OF_MON_TYPES; attack++)
    {
        DecodeGameString(name, sizeof(name), gTypesInfo[attack].name, TYPE_NAME_LENGTH);
        JsonPut(&writer, attack == 0 ? "" : ",");
        JsonPutString(&writer, name);
    }
    JsonPut(&writer, "],\"chart\":[");
    for (attack = 0; attack < NUMBER_OF_MON_TYPES; attack++)
    {
        JsonPut(&writer, "%s[", attack == 0 ? "" : ",");
        for (defence = 0; defence < NUMBER_OF_MON_TYPES; defence++)
            JsonPut(&writer, "%s%u", defence == 0 ? "" : ",", gTypeEffectivenessTable[attack][defence]);
        JsonPut(&writer, "]");
    }
    JsonPut(&writer, "]}");
    if (writer.overflowed)
    {
        dest[0] = '\0';
        return 0;
    }
    return writer.length;
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

static u32 Rgb555ToArgb(u16 color)
{
    u32 r = color & 0x1F, g = (color >> 5) & 0x1F, b = (color >> 10) & 0x1F;

    return 0xFF000000 | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}

// 4bpp tiles laid out tilesWide x tilesHigh, as sprites keep them, to ARGB.
// Colour 0 is transparent.
static void TilesToArgb(const u8 *tiles, const u16 *palette, u32 tilesWide, u32 tilesHigh, u32 *argb)
{
    u32 width = tilesWide * 8;
    u32 x, y;

    for (y = 0; y < tilesHigh * 8; y++)
    {
        for (x = 0; x < width; x++)
        {
            const u8 *tile = tiles + ((y / 8) * tilesWide + (x / 8)) * 32;
            u8 pair = tile[(y % 8) * 4 + (x % 8) / 2];
            u8 index = (x & 1) ? (pair >> 4) : (pair & 0xF);

            argb[y * width + x] = index == 0 ? 0 : Rgb555ToArgb(palette[index]);
        }
    }
}

// The palette the game gives a menu icon (CreateMonIcon in pokemon_icon.c).
static u32 MonIconPalette(u32 species, u32 personality, bool32 isEgg)
{
    if (isEgg)
    {
        if (gSpeciesInfo[species].eggId != EGG_ID_NONE)
            return gEggDatas[gSpeciesInfo[species].eggId].eggIconPalIndex;
        return gSpeciesInfo[SPECIES_EGG].iconPalIndex;
    }
#if P_GENDER_DIFFERENCES
    if (gSpeciesInfo[species].iconSpriteFemale != NULL && IsPersonalityFemale(species, personality))
        return gSpeciesInfo[species].iconPalIndexFemale;
#endif
    return gSpeciesInfo[species].iconPalIndex;
}

bool32 DualScreen_MonIcon(u32 species, u32 personality, bool32 isEgg, u32 *argb)
{
    const u8 *tiles;
    u32 palette;

    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return FALSE;
    // Unown's letter comes from its personality, as in GetMonIconPtrIsEgg.
    if (!isEgg)
        species = GetIconSpecies(species, personality);
    tiles = GetMonIconTilesIsEgg(species, personality, isEgg);
    if (tiles == NULL)
        return FALSE;
    palette = MonIconPalette(species, personality, isEgg);
    if (palette >= DS_MON_ICON_PALETTES)
        palette = 0;
    TilesToArgb(tiles, gMonIconPalettes[palette], 4, 4, argb);
    return TRUE;
}

bool32 DualScreen_ItemIcon(u32 item, u32 *argb)
{
    u32 i;
    bool32 found = FALSE;

    if (item == ITEM_NONE || item >= ITEMS_COUNT)
        return FALSE;
    pthread_mutex_lock(&sLock);
    for (i = 0; i < DS_ITEM_ICON_CACHE; i++)
    {
        if (sItemIcons[i].item == item)
        {
            memcpy(argb, sItemIcons[i].argb, sizeof(sItemIcons[i].argb));
            found = TRUE;
            break;
        }
    }
    if (!found)
    {
        for (i = 0; i < sItemIconQueueCount && sItemIconQueue[i] != item; i++)
            ;
        if (i == sItemIconQueueCount && sItemIconQueueCount < DS_ITEM_ICON_QUEUE)
            sItemIconQueue[sItemIconQueueCount++] = item;
    }
    pthread_mutex_unlock(&sLock);
    return found;
}

// Game thread: decodes a few of the item icons asked for.
static void DecodeItemIcons(void)
{
    static u8 sTiles[512]; // 3x3 tiles, 288 bytes, with room to spare
    static u32 sArgb[DS_ITEM_ICON_SIZE * DS_ITEM_ICON_SIZE];
    u32 done;

    for (done = 0; done < DS_ITEM_ICONS_PER_FRAME; done++)
    {
        const u32 *pic;
        const u16 *palette;
        u16 item;

        pthread_mutex_lock(&sLock);
        if (sItemIconQueueCount == 0)
        {
            pthread_mutex_unlock(&sLock);
            return;
        }
        item = sItemIconQueue[0];
        memmove(sItemIconQueue, sItemIconQueue + 1, (sItemIconQueueCount - 1) * sizeof(sItemIconQueue[0]));
        sItemIconQueueCount--;
        pthread_mutex_unlock(&sLock);

        // Through the game's accessors: TMs and HMs share a disc picture
        // coloured by the taught move's type.
        pic = GetItemIconPic(item);
        palette = GetItemIconPalette(item);
        if (pic == NULL || palette == NULL)
        {
            pic = gItemsInfo[ITEM_NONE].iconPic;
            palette = gItemsInfo[ITEM_NONE].iconPalette;
        }
        if (pic == NULL || palette == NULL)
            memset(sArgb, 0, sizeof(sArgb));
        else
        {
            DecompressDataWithHeaderWram(pic, sTiles);
            TilesToArgb(sTiles, palette, 3, 3, sArgb);
        }

        pthread_mutex_lock(&sLock);
        sItemIcons[sItemIconNext].item = item;
        memcpy(sItemIcons[sItemIconNext].argb, sArgb, sizeof(sArgb));
        sItemIconNext = (sItemIconNext + 1) % DS_ITEM_ICON_CACHE;
        pthread_mutex_unlock(&sLock);
    }
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
    bool32 bagWanted;

    sFrame++;
    DriveTap();
    if (sFrame % DS_PUBLISH_INTERVAL == 0)
        PublishSnapshot();
    pthread_mutex_lock(&sLock);
    bagWanted = sBagWanted;
    pthread_mutex_unlock(&sLock);
    if (bagWanted && sFrame % DS_BAG_INTERVAL == 0)
        PublishBag();
    DecodeItemIcons();
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
        if (getenv("HNS_BAG_DUMP") != NULL)
        {
            PublishBag();
            fprintf(stderr, "bag %lu %s\n", frame, sPublishedBagJson);
        }
        if (frame == sDumpEvery)
        {
            static char sChart[8192];

            DualScreen_TypeChartJson(sChart, sizeof(sChart));
            fprintf(stderr, "types %s\n", sChart);
        }
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

JNIEXPORT jstring JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeGetBagJson(JNIEnv *env, jclass clazz)
{
    static char sJniBagJson[DS_BAG_JSON_CAPACITY]; // only the UI thread calls this

    (void)clazz;
    DualScreen_CopyBagJson(sJniBagJson, sizeof(sJniBagJson));
    return (*env)->NewStringUTF(env, sJniBagJson);
}

JNIEXPORT void JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeSetBagWanted(JNIEnv *env, jclass clazz, jboolean wanted)
{
    (void)env;
    (void)clazz;
    DualScreen_SetBagWanted(wanted != JNI_FALSE);
}

JNIEXPORT jstring JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeGetTypeChartJson(JNIEnv *env, jclass clazz)
{
    char json[8192];

    (void)clazz;
    DualScreen_TypeChartJson(json, sizeof(json));
    return (*env)->NewStringUTF(env, json);
}

// The icon as ARGB pixels, or null when there is none (yet, for items).
static jintArray IconToJava(JNIEnv *env, const u32 *argb, u32 count)
{
    jintArray result = (*env)->NewIntArray(env, count);

    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, count, (const jint *)argb);
    return result;
}

JNIEXPORT jintArray JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeGetMonIcon(JNIEnv *env, jclass clazz, jint species, jint personality, jboolean egg)
{
    u32 argb[32 * 32];

    (void)clazz;
    if (species < 0 || !DualScreen_MonIcon(species, (u32)personality, egg != JNI_FALSE, argb))
        return NULL;
    return IconToJava(env, argb, ARRAY_COUNT(argb));
}

JNIEXPORT jintArray JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeGetItemIcon(JNIEnv *env, jclass clazz, jint item)
{
    u32 argb[DS_ITEM_ICON_SIZE * DS_ITEM_ICON_SIZE];

    (void)clazz;
    if (item < 0 || !DualScreen_ItemIcon(item, argb))
        return NULL;
    return IconToJava(env, argb, ARRAY_COUNT(argb));
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
