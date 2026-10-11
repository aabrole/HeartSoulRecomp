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
#include "event_data.h"
#include "mom_savings.h"
#include "overworld.h"
#include "pokedex.h"
#include "randomizer.h"
#include "rtc.h"
#include "wild_encounter.h"
#include "constants/pokedex.h"
#include "constants/region_map_sections.h"
#include "graphics.h"
#include "item.h"
#include "item_icon.h"
#include "money.h"
#include "move.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "region_map.h"
#include "start_menu.h"
#include "strings.h"
#include "task.h"
#include "platform/dualscreen.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"

#define DS_JSON_CAPACITY 32768
#define DS_PUBLISH_INTERVAL 4   // frames between snapshots
#define DS_DOC_JSON_CAPACITY (192 * 1024) // a full bag with every description is about 66K
#define DS_DOC_IMAGE_MAX (256 * 256)      // pixels: the region map is 256x160
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
static char sPublishedDoc[DS_DOC_JSON_CAPACITY] = "{\"doc\":0}";
static u32 sPublishedImage[DS_DOC_IMAGE_MAX];
static u32 sPublishedImageWidth;
static u32 sPublishedImageHeight;
static u32 sWantedDoc;
static u32 sWantedParam;
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
static char sWorkDoc[DS_DOC_JSON_CAPACITY];
static u32 sWorkImage[DS_DOC_IMAGE_MAX];
static u32 sDocKind;     // the document last published, and its param
static u32 sDocParam;
static u32 sDocImageKey; // what its picture was drawn for (DocImageKey)
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

            struct DualScreenFoeMon *foeMon;

            if (species == SPECIES_NONE || species == SPECIES_EGG || species >= NUM_SPECIES)
                continue;
            foeMon = &dest->foeTeam[dest->foeTeamCount++];
            foeMon->species = species;
            foeMon->personality = GetMonData(&gEnemyParty[i], MON_DATA_PERSONALITY);
            foeMon->level = GetMonData(&gEnemyParty[i], MON_DATA_LEVEL);
            foeMon->hp = GetMonData(&gEnemyParty[i], MON_DATA_HP);
            foeMon->maxHp = GetMonData(&gEnemyParty[i], MON_DATA_MAX_HP);
            DecodeGameString(foeMon->speciesName, sizeof(foeMon->speciesName), GetSpeciesName(species),
                             POKEMON_NAME_LENGTH);
            if (foeMon->hp != 0)
                dest->foeTeamLeft++;
        }
    }
}

// What the game's trainer card shows (SetPlayerCardData in trainer_card.c).
static void FillTrainerCard(struct DualScreenSnapshot *snapshot)
{
    u32 i;

    snapshot->trainerId = (gSaveBlock2Ptr->playerTrainerId[1] << 8) | gSaveBlock2Ptr->playerTrainerId[0];
    snapshot->playerGender = gSaveBlock2Ptr->playerGender;
    snapshot->hasPokedex = FlagGet(FLAG_SYS_POKEDEX_GET);
    snapshot->nationalDex = IsNationalPokedexEnabled();
    snapshot->dexSeen = snapshot->nationalDex ? GetNationalPokedexCount(FLAG_GET_SEEN) : GetJohtoPokedexCount(FLAG_GET_SEEN);
    snapshot->dexCaught = snapshot->nationalDex ? GetNationalPokedexCount(FLAG_GET_CAUGHT) : GetJohtoPokedexCount(FLAG_GET_CAUGHT);
    for (i = 0; i < NUM_BADGES && i < 16; i++)
    {
        if (FlagGet(FLAG_BADGE01_GET + i))
            snapshot->badgeMask |= 1u << i;
    }
    snapshot->momMoney = Mom_GetBalance();
    // The stars, as GetHnSTrainerStars counts them.
    snapshot->stars = (FlagGet(FLAG_IS_CHAMPION) ? 1 : 0) + (HasAllRegionalMons() ? 1 : 0)
                    + (FlagGet(TRAINER_FLAGS_START + TRAINER_RED_HNS) ? 1 : 0)
                    + (FlagGet(FLAG_IS_KANTO_CHAMPION) ? 1 : 0);
    snapshot->hofTime = GetGameStat(GAME_STAT_FIRST_HOF_PLAY_TIME);
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
    FillTrainerCard(snapshot);
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
        JsonPut(&writer, ",\"id\":%u,\"gender\":%u,\"hasDex\":%s,\"nationalDex\":%s,\"dexSeen\":%u,\"dexCaught\":%u,"
                "\"badgeMask\":%u,\"momMoney\":%u,\"stars\":%u,\"hofTime\":%u}",
                snapshot->trainerId, snapshot->playerGender, snapshot->hasPokedex ? "true" : "false",
                snapshot->nationalDex ? "true" : "false", snapshot->dexSeen, snapshot->dexCaught,
                snapshot->badgeMask, (unsigned)snapshot->momMoney, snapshot->stars, (unsigned)snapshot->hofTime);
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
        {
            JsonPut(&writer, ",\"foeTeam\":%u,\"foeTeamLeft\":%u,\"foeParty\":[", battle->foeTeamCount,
                    battle->foeTeamLeft);
            for (i = 0; i < battle->foeTeamCount && i < PARTY_SIZE; i++)
            {
                const struct DualScreenFoeMon *foeMon = &battle->foeTeam[i];

                JsonPut(&writer, "%s{\"speciesId\":%u,\"personality\":%u,\"lv\":%u,\"hp\":%u,\"maxHp\":%u,\"species\":",
                        i == 0 ? "" : ",", foeMon->species, (unsigned)foeMon->personality, foeMon->level,
                        foeMon->hp, foeMon->maxHp);
                JsonPutString(&writer, foeMon->speciesName);
                JsonPut(&writer, "}");
            }
            JsonPut(&writer, "]");
        }
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
// Smart fast forward
// ---------------------------------------------------------------------------

bool32 DualScreen_IsSlowScreen(void)
{
    if (gMain.callback2 == CB2_Overworld)
        return FuncIsActiveTask(Task_ShowStartMenu);
    if (gMain.callback2 == BattleMainCB2)
        return FALSE;
    return TRUE;
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
// Documents: bag, guide, Pokédex, map
// ---------------------------------------------------------------------------

// The pockets in the bag screen's order, each with its items in bag order.
static void BagToJson(struct JsonWriter *writer)
{
    char text[256];
    u32 pocketId, slot;

    JsonPut(writer, ",\"pockets\":[");
    for (pocketId = 0; pocketId < POCKETS_COUNT; pocketId++)
    {
        struct BagPocket *pocket = &gBagPockets[pocketId];
        bool32 first = TRUE;

        DecodeGameString(text, sizeof(text), gPocketNamesStringsTable[pocketId], 20);
        JsonPut(writer, "%s{\"id\":%u,\"name\":", pocketId == 0 ? "" : ",", pocketId);
        JsonPutString(writer, text);
        JsonPut(writer, ",\"items\":[");
        for (slot = 0; pocket->itemSlots != NULL && slot < pocket->capacity; slot++)
        {
            struct ItemSlot item = BagPocket_GetSlotData(pocket, slot);
            enum Move move;

            if (item.itemId == ITEM_NONE || item.itemId >= ITEMS_COUNT || item.quantity == 0)
                continue;
            JsonPut(writer, "%s{\"id\":%u,\"qty\":%u,\"name\":", first ? "" : ",", item.itemId, item.quantity);
            first = FALSE;
            DecodeGameString(text, sizeof(text), GetItemName(item.itemId), ITEM_NAME_LENGTH);
            JsonPutString(writer, text);
            move = pocketId == POCKET_TM_HM ? GetItemTMHMMoveId(item.itemId) : MOVE_NONE;
            if (move != MOVE_NONE && move < MOVES_COUNT)
            {
                u32 type = GetMoveType(move);

                DecodeGameString(text, sizeof(text), GetMoveName(move), MOVE_NAME_LENGTH);
                JsonPut(writer, ",\"move\":");
                JsonPutString(writer, text);
                JsonPut(writer, ",\"typeId\":%u", type < NUMBER_OF_MON_TYPES ? type : TYPE_NONE);
            }
            DecodeGameText(text, sizeof(text), GetItemDescription(item.itemId), 200);
            JsonPut(writer, ",\"desc\":");
            JsonPutString(writer, text);
            JsonPut(writer, "}");
        }
        JsonPut(writer, "]}");
    }
    JsonPut(writer, "]");
}

static bool32 IsSpeciesCaught(u32 species)
{
    return species < NUM_SPECIES && GetSetPokedexFlag(SpeciesToNationalPokedexNum(species), FLAG_GET_CAUGHT);
}

static bool32 IsSpeciesSeen(u32 species)
{
    return species < NUM_SPECIES && GetSetPokedexFlag(SpeciesToNationalPokedexNum(species), FLAG_GET_SEEN);
}

// --- Guide: wild Pokémon here ---

// The game's chance for each slot, in percent (ChooseWildMonIndex_*).
static const u8 sLandSlotRates[12] = {20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1};
static const u8 sWaterSlotRates[5] = {60, 30, 5, 4, 1};
// Fishing: old rod slots 0-1, good rod 2-4, super rod 5-9.
static const u8 sFishingSlotRates[10] = {70, 30, 60, 20, 20, 40, 40, 15, 4, 1};

struct GuideMon
{
    u16 species;
    u8 minLevel;
    u8 maxLevel;
    u8 rate;
};

// One way of meeting wild Pokémon, its species merged with their chances.
static void GuideAreaToJson(struct JsonWriter *writer, bool32 *first, const char *name,
                            const struct WildPokemonInfo *info, const u8 *rates, u32 firstSlot, u32 slotCount)
{
    struct GuideMon mons[12];
    u32 count = 0;
    u32 slot, i;
    char text[DS_TEXT_LENGTH];

    if (info == NULL || info->wildPokemon == NULL)
        return;
    for (slot = firstSlot; slot < firstSlot + slotCount; slot++)
    {
        const struct WildPokemon *wild = &info->wildPokemon[slot];

        if (wild->species == SPECIES_NONE || wild->species >= NUM_SPECIES || !IsSpeciesEnabled(wild->species))
            continue;
        for (i = 0; i < count && mons[i].species != wild->species; i++)
            ;
        if (i == count)
        {
            if (count == ARRAY_COUNT(mons))
                continue;
            mons[count].species = wild->species;
            mons[count].minLevel = wild->minLevel;
            mons[count].maxLevel = wild->maxLevel;
            mons[count].rate = 0;
            count++;
        }
        mons[i].minLevel = min(mons[i].minLevel, wild->minLevel);
        mons[i].maxLevel = max(mons[i].maxLevel, wild->maxLevel);
        mons[i].rate += rates[slot];
    }
    if (count == 0)
        return;

    JsonPut(writer, "%s{\"name\":\"%s\",\"mons\":[", *first ? "" : ",", name);
    *first = FALSE;
    for (i = 0; i < count; i++)
    {
        DecodeGameString(text, sizeof(text), GetSpeciesName(mons[i].species), POKEMON_NAME_LENGTH);
        JsonPut(writer, "%s{\"speciesId\":%u,\"species\":", i == 0 ? "" : ",", mons[i].species);
        JsonPutString(writer, text);
        JsonPut(writer, ",\"min\":%u,\"max\":%u,\"rate\":%u,\"seen\":%s,\"caught\":%s}",
                mons[i].minLevel, mons[i].maxLevel, mons[i].rate,
                IsSpeciesSeen(mons[i].species) ? "true" : "false",
                IsSpeciesCaught(mons[i].species) ? "true" : "false");
    }
    JsonPut(writer, "]}");
}

static void GuideWildToJson(struct JsonWriter *writer)
{
    static const char *const sTimeNames[] = { "MORNING", "DAY", "EVENING", "NIGHT" };
    u16 headerId = GetCurrentMapWildMonHeaderId();
    const struct WildEncounterTypes *types;
    bool32 first = TRUE;
    u32 time;

    // Randomized wild Pokémon cannot be told without drawing from the
    // game's random numbers, which would change the game.
    if (RandomizerFeatureEnabled(RANDOMIZE_WILD_MON))
    {
        JsonPut(writer, ",\"wildRandomized\":true,\"here\":[]");
        return;
    }
    JsonPut(writer, ",\"here\":[");
    if (headerId != HEADER_NONE)
    {
        time = GetTimeOfDayForEncounters(headerId, WILD_AREA_LAND);
        types = &gWildMonHeaders[headerId].encounterTypes[time < TIMES_OF_DAY_COUNT ? time : 0];
        GuideAreaToJson(writer, &first, "GRASS AND CAVES", types->landMonsInfo, sLandSlotRates, 0, 12);

        time = GetTimeOfDayForEncounters(headerId, WILD_AREA_WATER);
        types = &gWildMonHeaders[headerId].encounterTypes[time < TIMES_OF_DAY_COUNT ? time : 0];
        GuideAreaToJson(writer, &first, "SURFING", types->waterMonsInfo, sWaterSlotRates, 0, 5);

        time = GetTimeOfDayForEncounters(headerId, WILD_AREA_ROCKS);
        types = &gWildMonHeaders[headerId].encounterTypes[time < TIMES_OF_DAY_COUNT ? time : 0];
        GuideAreaToJson(writer, &first, "ROCK SMASH", types->rockSmashMonsInfo, sWaterSlotRates, 0, 5);

        time = GetTimeOfDayForEncounters(headerId, WILD_AREA_FISHING);
        types = &gWildMonHeaders[headerId].encounterTypes[time < TIMES_OF_DAY_COUNT ? time : 0];
        GuideAreaToJson(writer, &first, "OLD ROD", types->fishingMonsInfo, sFishingSlotRates, 0, 2);
        GuideAreaToJson(writer, &first, "GOOD ROD", types->fishingMonsInfo, sFishingSlotRates, 2, 3);
        GuideAreaToJson(writer, &first, "SUPER ROD", types->fishingMonsInfo, sFishingSlotRates, 5, 5);
    }
    JsonPut(writer, "]");
    time = GetTimeOfDay();
    JsonPut(writer, ",\"time\":\"%s\"", time < ARRAY_COUNT(sTimeNames) ? sTimeNames[time] : "");
}

// --- Guide: the next boss ---

struct Boss
{
    const char *place;
    u16 badgeFlag;  // the badge that beating them leads to
    u16 trainer;    // the team before any of the other two middle gyms
    u16 trainerOne; // the middle gyms: after one of the other two
    u16 trainerTwo; // and after both
    u16 otherA;     // the middle gyms: the other two gyms' defeat flags
    u16 otherB;
};

static const struct Boss sJohtoBosses[] =
{
    {"VIOLET CITY GYM",    FLAG_BADGE01_GET,     TRAINER_FALKNER_1_HNS},
    {"AZALEA TOWN GYM",    FLAG_BADGE01_GET + 1, TRAINER_BUGSY_1_HNS},
    {"GOLDENROD CITY GYM", FLAG_BADGE01_GET + 2, TRAINER_WHITNEY_1_HNS},
    {"ECRUTEAK CITY GYM",  FLAG_BADGE01_GET + 3, TRAINER_MORTY_1_HNS},
    {"CIANWOOD CITY GYM",  FLAG_BADGE01_GET + 4, TRAINER_CHUCK_1_HNS, TRAINER_CHUCK_1_2_HNS, TRAINER_CHUCK_1_3_HNS,
     FLAG_DEFEATED_OLIVINE_CITY_GYM, FLAG_DEFEATED_MAHOGANY_TOWN_GYM},
    {"OLIVINE CITY GYM",   FLAG_BADGE01_GET + 5, TRAINER_JASMINE_1_HNS, TRAINER_JASMINE_1_2_HNS, TRAINER_JASMINE_1_3_HNS,
     FLAG_DEFEATED_CIANWOOD_GYM, FLAG_DEFEATED_MAHOGANY_TOWN_GYM},
    {"MAHOGANY TOWN GYM",  FLAG_BADGE01_GET + 6, TRAINER_PRYCE_1_HNS, TRAINER_PRYCE_1_2_HNS, TRAINER_PRYCE_1_3_HNS,
     FLAG_DEFEATED_CIANWOOD_GYM, FLAG_DEFEATED_OLIVINE_CITY_GYM},
    {"BLACKTHORN CITY GYM", FLAG_BADGE01_GET + 7, TRAINER_CLAIR_1_HNS},
};

static const struct Boss sKantoBosses[] =
{
    {"PEWTER CITY GYM",    FLAG_BADGE01_GET + 8,  TRAINER_BROCK_HNS},
    {"CERULEAN CITY GYM",  FLAG_BADGE01_GET + 9,  TRAINER_MISTY_HNS},
    {"VERMILION CITY GYM", FLAG_BADGE01_GET + 10, TRAINER_LTSURGE_HNS},
    {"CELADON CITY GYM",   FLAG_BADGE01_GET + 11, TRAINER_ERIKA_HNS},
    {"SAFFRON CITY GYM",   FLAG_BADGE01_GET + 12, TRAINER_SABRINA_HNS},
    {"FUCHSIA CITY GYM",   FLAG_BADGE01_GET + 13, TRAINER_JANINE_HNS},
    {"SEAFOAM GYM",        FLAG_BADGE01_GET + 14, TRAINER_BLAINE_HNS},
    {"VIRIDIAN CITY GYM",  FLAG_BADGE01_GET + 15, TRAINER_BLUE_HNS},
};

// The Elite Four and the champion in order, first run and rematch.
static const u16 sLeagueFirst[] = {TRAINER_WILL_1_HNS, TRAINER_KOGA_1_HNS, TRAINER_BRUNO_1_HNS,
                                   TRAINER_KAREN_1_HNS, TRAINER_LANCE_1_HNS};
static const u16 sLeagueSecond[] = {TRAINER_WILL_2_HNS, TRAINER_KOGA_2_HNS, TRAINER_BRUNO_2_HNS,
                                    TRAINER_KAREN_2_HNS, TRAINER_LANCE_2_HNS};

static u16 BossTrainer(const struct Boss *boss)
{
    u32 beaten;

    if (boss->trainerOne == 0)
        return boss->trainer;
    beaten = (FlagGet(boss->otherA) ? 1 : 0) + (FlagGet(boss->otherB) ? 1 : 0);
    return beaten == 0 ? boss->trainer : beaten == 1 ? boss->trainerOne : boss->trainerTwo;
}

// The next trainer the story puts in the way, or 0 when there is none left.
static u16 NextBoss(const char **place)
{
    u32 i, state;

    for (i = 0; i < ARRAY_COUNT(sJohtoBosses); i++)
    {
        if (!FlagGet(sJohtoBosses[i].badgeFlag))
        {
            *place = sJohtoBosses[i].place;
            return BossTrainer(&sJohtoBosses[i]);
        }
    }
    // VAR_LEAGUE_STATE counts the League's rooms: 1 on entry, 2 after WILL...
    state = VarGet(VAR_LEAGUE_STATE);
    state = state > 1 ? state - 1 : 0;
    if (state >= ARRAY_COUNT(sLeagueFirst))
        state = ARRAY_COUNT(sLeagueFirst) - 1;
    if (!FlagGet(FLAG_IS_CHAMPION))
    {
        *place = "INDIGO PLATEAU";
        return sLeagueFirst[state];
    }
    for (i = 0; i < ARRAY_COUNT(sKantoBosses); i++)
    {
        if (!FlagGet(sKantoBosses[i].badgeFlag))
        {
            *place = sKantoBosses[i].place;
            return BossTrainer(&sKantoBosses[i]);
        }
    }
    if (!FlagGet(FLAG_IS_KANTO_CHAMPION))
    {
        *place = "INDIGO PLATEAU";
        return sLeagueSecond[state];
    }
    if (!FlagGet(TRAINER_FLAGS_START + TRAINER_RED_POSTOBC_HNS))
    {
        *place = "MT. SILVER";
        return TRAINER_RED_POSTOBC_HNS;
    }
    return 0;
}

static void GuideBossToJson(struct JsonWriter *writer)
{
    const char *place = "";
    u16 trainerId = NextBoss(&place);
    const struct Trainer *trainer;
    const struct TrainerMon *party;
    char text[DS_TEXT_LENGTH];
    u32 size, i, j;
    bool32 firstMon = TRUE;

    if (trainerId == 0)
    {
        JsonPut(writer, ",\"boss\":null");
        return;
    }
    trainer = GetTrainerStructFromId(trainerId);
    DecodeGameString(text, sizeof(text), GetTrainerClassNameFromId(trainerId), 13);
    JsonPut(writer, ",\"boss\":{\"class\":");
    JsonPutString(writer, text);
    DecodeGameString(text, sizeof(text), trainer->trainerName, TRAINER_NAME_LENGTH);
    JsonPut(writer, ",\"name\":");
    JsonPutString(writer, text);
    JsonPut(writer, ",\"place\":");
    JsonPutString(writer, place);

    // A trainer can take its team from another (CreateNPCTrainerParty).
    if (trainer->overrideTrainer)
    {
        const struct Trainer *source = GetTrainerStructFromId(trainer->overrideTrainer);

        party = source->party;
        size = trainer->partySize != 0 ? trainer->partySize : source->partySize;
        if (source->poolSize != 0)
            size = source->poolSize;
    }
    else
    {
        party = trainer->party;
        size = trainer->poolSize != 0 ? trainer->poolSize : trainer->partySize;
    }
    // From a pool the game picks partySize of them; show the whole pool.
    JsonPut(writer, ",\"pool\":%s,\"randomized\":%s,\"team\":[",
            trainer->poolSize != 0 ? "true" : "false",
            RandomizerFeatureEnabled(RANDOMIZE_TRAINER_MON) ? "true" : "false");
    for (i = 0; party != NULL && i < size && i < 12; i++)
    {
        const struct TrainerMon *mon = &party[i];
        bool32 firstMove = TRUE;

        if (mon->species == SPECIES_NONE || mon->species >= NUM_SPECIES || !IsSpeciesEnabled(mon->species))
            continue;
        DecodeGameString(text, sizeof(text), GetSpeciesName(mon->species), POKEMON_NAME_LENGTH);
        JsonPut(writer, "%s{\"speciesId\":%u,\"species\":", firstMon ? "" : ",", mon->species);
        firstMon = FALSE;
        JsonPutString(writer, text);
        JsonPut(writer, ",\"lv\":%u,\"item\":", mon->lvl);
        text[0] = '\0';
        if (mon->heldItem != ITEM_NONE && mon->heldItem < ITEMS_COUNT)
            DecodeGameString(text, sizeof(text), GetItemName(mon->heldItem), ITEM_NAME_LENGTH);
        JsonPutString(writer, text);
        JsonPut(writer, ",\"types\":[%u,%u],\"moves\":[", GetSpeciesType(mon->species, 0),
                GetSpeciesType(mon->species, 1));
        for (j = 0; j < MAX_MON_MOVES; j++)
        {
            if (mon->moves[j] == MOVE_NONE || mon->moves[j] >= MOVES_COUNT)
                continue;
            DecodeGameString(text, sizeof(text), GetMoveName(mon->moves[j]), MOVE_NAME_LENGTH);
            JsonPut(writer, firstMove ? "" : ",");
            JsonPutString(writer, text);
            firstMove = FALSE;
        }
        JsonPut(writer, "]}");
    }
    JsonPut(writer, "]}");
}

static void GuideToJson(struct JsonWriter *writer)
{
    u8 mapName[64];
    char text[DS_TEXT_LENGTH];

    mapName[0] = EOS;
    GetMapName(mapName, gMapHeader.regionMapSectionId, 0);
    DecodeGameString(text, sizeof(text), mapName, sizeof(mapName) - 1);
    JsonPut(writer, ",\"map\":");
    JsonPutString(writer, text);
    GuideWildToJson(writer);
    GuideBossToJson(writer);
}

// --- Pokédex ---

// The dex in the order the game shows it: national once the national dex is
// on, Johto before. Names and types only for species seen.
static void DexToJson(struct JsonWriter *writer)
{
    bool32 national = IsNationalPokedexEnabled();
    u32 count = national ? NATIONAL_DEX_COUNT : JOHTO_DEX_COUNT - 1;
    char text[DS_TEXT_LENGTH];
    u32 i;
    bool32 first = TRUE;

    JsonPut(writer, ",\"national\":%s,\"entries\":[", national ? "true" : "false");
    for (i = 1; i <= count; i++)
    {
        u32 nationalNum = national ? i : JohtoToNationalOrder(i);
        u32 species = NationalPokedexNumToSpecies(nationalNum);
        bool32 seen, caught;

        if (species == SPECIES_NONE || species >= NUM_SPECIES || !IsSpeciesEnabled(species))
            continue;
        seen = GetSetPokedexFlag(nationalNum, FLAG_GET_SEEN);
        caught = GetSetPokedexFlag(nationalNum, FLAG_GET_CAUGHT);
        JsonPut(writer, "%s{\"n\":%u,\"s\":%u", first ? "" : ",", i, species);
        first = FALSE;
        if (seen || caught)
        {
            DecodeGameString(text, sizeof(text), GetSpeciesName(species), POKEMON_NAME_LENGTH);
            JsonPut(writer, ",\"name\":");
            JsonPutString(writer, text);
            JsonPut(writer, ",\"types\":[%u,%u],\"seen\":true", GetSpeciesType(species, 0), GetSpeciesType(species, 1));
        }
        if (caught)
            JsonPut(writer, ",\"caught\":true");
        JsonPut(writer, "}");
    }
    JsonPut(writer, "],\"seenCount\":%u,\"caughtCount\":%u",
            national ? GetNationalPokedexCount(FLAG_GET_SEEN) : GetJohtoPokedexCount(FLAG_GET_SEEN),
            national ? GetNationalPokedexCount(FLAG_GET_CAUGHT) : GetJohtoPokedexCount(FLAG_GET_CAUGHT));
}

static const char *TimeOfDayName(u32 time)
{
    static const char *const sNames[] = { "MORNING", "DAY", "EVENING", "NIGHT" };

    return time < ARRAY_COUNT(sNames) ? sNames[time] : "";
}

// How an evolution happens, in a few words: "LV 16", "FIRE STONE",
// "TRADE", with the first extra condition ("LV UP + FRIENDSHIP").
static void EvolutionToText(char *dest, int capacity, const struct Evolution *evolution)
{
    char item[DS_TEXT_LENGTH];
    int length;

    switch (evolution->method)
    {
    case EVO_LEVEL:
        if (evolution->param > 1)
            length = snprintf(dest, capacity, "LV %u", evolution->param);
        else
            length = snprintf(dest, capacity, "LEVEL UP");
        break;
    case EVO_LEVEL_BATTLE_ONLY:
        length = snprintf(dest, capacity, "LV %u IN BATTLE", evolution->param);
        break;
    case EVO_TRADE:
        length = snprintf(dest, capacity, "TRADE");
        break;
    case EVO_ITEM:
        item[0] = '\0';
        if (evolution->param < ITEMS_COUNT)
            DecodeGameString(item, sizeof(item), GetItemName(evolution->param), ITEM_NAME_LENGTH);
        length = snprintf(dest, capacity, "%s", item);
        break;
    default:
        length = snprintf(dest, capacity, "SPECIAL");
        break;
    }
    if (length < 0 || length >= capacity || evolution->params == NULL
     || evolution->params[0].condition == CONDITIONS_END)
        return;

    switch (evolution->params[0].condition)
    {
    case IF_MIN_FRIENDSHIP:
        snprintf(dest + length, capacity - length, " + FRIENDSHIP");
        break;
    case IF_TIME:
        snprintf(dest + length, capacity - length, " + %s", TimeOfDayName(evolution->params[0].arg1));
        break;
    case IF_NOT_TIME:
        snprintf(dest + length, capacity - length, " + NOT %s", TimeOfDayName(evolution->params[0].arg1));
        break;
    case IF_GENDER:
        snprintf(dest + length, capacity - length, " + %s", evolution->params[0].arg1 == MON_MALE ? "♂" : "♀");
        break;
    case IF_HOLD_ITEM:
        item[0] = '\0';
        if (evolution->params[0].arg1 < ITEMS_COUNT)
            DecodeGameString(item, sizeof(item), GetItemName(evolution->params[0].arg1), ITEM_NAME_LENGTH);
        snprintf(dest + length, capacity - length, " + HOLD %s", item);
        break;
    case IF_KNOWS_MOVE:
        item[0] = '\0';
        if (evolution->params[0].arg1 < MOVES_COUNT)
            DecodeGameString(item, sizeof(item), GetMoveName(evolution->params[0].arg1), MOVE_NAME_LENGTH);
        snprintf(dest + length, capacity - length, " + KNOWS %s", item);
        break;
    case IF_ATK_GT_DEF:
        snprintf(dest + length, capacity - length, " + ATK > DEF");
        break;
    case IF_ATK_EQ_DEF:
        snprintf(dest + length, capacity - length, " + ATK = DEF");
        break;
    case IF_ATK_LT_DEF:
        snprintf(dest + length, capacity - length, " + ATK < DEF");
        break;
    default:
        snprintf(dest + length, capacity - length, " + SPECIAL");
        break;
    }
}

static void JsonPutSpeciesRef(struct JsonWriter *writer, u32 species)
{
    char text[DS_TEXT_LENGTH];

    DecodeGameString(text, sizeof(text), GetSpeciesName(species), POKEMON_NAME_LENGTH);
    JsonPut(writer, "\"speciesId\":%u,\"species\":", species);
    JsonPutString(writer, text);
}

// What it evolves from (searched for) and into.
static void DexEvolutionsToJson(struct JsonWriter *writer, u32 species)
{
    char text[DS_TEXT_LENGTH];
    const struct Evolution *evolutions;
    bool32 first = TRUE;
    u32 other, i;

    JsonPut(writer, ",\"from\":[");
    for (other = 1; other < NUM_SPECIES; other++)
    {
        // The game asserts on a disabled species.
        if (!IsSpeciesEnabled(other))
            continue;
        evolutions = GetSpeciesEvolutions(other);
        for (i = 0; evolutions != NULL && evolutions[i].method != EVOLUTIONS_END && i < 16; i++)
        {
            if (evolutions[i].targetSpecies != species || evolutions[i].method == EVO_NONE
             || evolutions[i].method == EVO_SPLIT_FROM_EVO)
                continue;
            EvolutionToText(text, sizeof(text), &evolutions[i]);
            JsonPut(writer, first ? "{" : ",{");
            JsonPutSpeciesRef(writer, other);
            JsonPut(writer, ",\"how\":");
            JsonPutString(writer, text);
            JsonPut(writer, "}");
            first = FALSE;
        }
    }
    JsonPut(writer, "],\"into\":[");
    first = TRUE;
    evolutions = GetSpeciesEvolutions(species);
    for (i = 0; evolutions != NULL && evolutions[i].method != EVOLUTIONS_END && i < 16; i++)
    {
        u32 target = evolutions[i].targetSpecies;

        if (target == SPECIES_NONE || target >= NUM_SPECIES || !IsSpeciesEnabled(target) || evolutions[i].method == EVO_NONE
         || evolutions[i].method == EVO_SPLIT_FROM_EVO)
            continue;
        EvolutionToText(text, sizeof(text), &evolutions[i]);
        JsonPut(writer, first ? "{" : ",{");
        JsonPutSpeciesRef(writer, target);
        JsonPut(writer, ",\"how\":");
        JsonPutString(writer, text);
        JsonPut(writer, "}");
        first = FALSE;
    }
    JsonPut(writer, "]");
}

// Every place and way it is met in the wild, merged by place and way.
static void DexWhereToJson(struct JsonWriter *writer, u32 species)
{
    static const char *const sAreaNames[] = { "GRASS", "SURF", "ROCK SMASH", "FISHING" };
    struct { u16 mapSec; u8 area; u8 minLevel; u8 maxLevel; } places[32];
    u32 count = 0;
    u32 header, time, area, slot, i;
    u8 mapName[64];
    char text[DS_TEXT_LENGTH];

    if (RandomizerFeatureEnabled(RANDOMIZE_WILD_MON))
    {
        JsonPut(writer, ",\"where\":null");
        return;
    }
    for (header = 0; gWildMonHeaders[header].mapGroup != MAP_GROUP(MAP_UNDEFINED); header++)
    {
        const struct MapHeader *mapHeader = Overworld_GetMapHeaderByGroupAndId(gWildMonHeaders[header].mapGroup,
                                                                              gWildMonHeaders[header].mapNum);
        u16 mapSec = mapHeader != NULL ? mapHeader->regionMapSectionId : MAPSEC_NONE;

        for (time = 0; time < TIMES_OF_DAY_COUNT; time++)
        {
            const struct WildEncounterTypes *types = &gWildMonHeaders[header].encounterTypes[time];
            const struct WildPokemonInfo *infos[] = { types->landMonsInfo, types->waterMonsInfo,
                                                      types->rockSmashMonsInfo, types->fishingMonsInfo };
            static const u8 sSlots[] = { 12, 5, 5, 10 };

            for (area = 0; area < ARRAY_COUNT(infos); area++)
            {
                if (infos[area] == NULL || infos[area]->wildPokemon == NULL)
                    continue;
                for (slot = 0; slot < sSlots[area]; slot++)
                {
                    const struct WildPokemon *wild = &infos[area]->wildPokemon[slot];

                    if (wild->species != species)
                        continue;
                    for (i = 0; i < count && !(places[i].mapSec == mapSec && places[i].area == area); i++)
                        ;
                    if (i == count)
                    {
                        if (count == ARRAY_COUNT(places))
                            continue;
                        places[count].mapSec = mapSec;
                        places[count].area = area;
                        places[count].minLevel = wild->minLevel;
                        places[count].maxLevel = wild->maxLevel;
                        count++;
                    }
                    places[i].minLevel = min(places[i].minLevel, wild->minLevel);
                    places[i].maxLevel = max(places[i].maxLevel, wild->maxLevel);
                }
            }
        }
    }

    JsonPut(writer, ",\"where\":[");
    for (i = 0; i < count; i++)
    {
        mapName[0] = EOS;
        if (places[i].mapSec < MAPSEC_NONE)
            GetMapName(mapName, places[i].mapSec, 0);
        DecodeGameString(text, sizeof(text), mapName, sizeof(mapName) - 1);
        JsonPut(writer, "%s{\"place\":", i == 0 ? "" : ",");
        JsonPutString(writer, text);
        JsonPut(writer, ",\"how\":\"%s\",\"min\":%u,\"max\":%u}", sAreaNames[places[i].area],
                places[i].minLevel, places[i].maxLevel);
    }
    JsonPut(writer, "]");
}

static void DexEntryToJson(struct JsonWriter *writer, u32 species)
{
    const struct SpeciesInfo *info;
    const struct LevelUpMove *learnset;
    char text[512];
    u32 i, nationalNum;
    bool32 first;

    if (species == SPECIES_NONE || species >= NUM_SPECIES || !IsSpeciesEnabled(species))
    {
        JsonPut(writer, ",\"species\":null");
        return;
    }
    info = &gSpeciesInfo[species];
    nationalNum = SpeciesToNationalPokedexNum(species);
    JsonPut(writer, ",");
    JsonPutSpeciesRef(writer, species);
    JsonPut(writer, ",\"national\":%u,\"johto\":%u,\"seen\":%s,\"caught\":%s",
            nationalNum, SpeciesToJohtoPokedexNum(species),
            GetSetPokedexFlag(nationalNum, FLAG_GET_SEEN) ? "true" : "false",
            GetSetPokedexFlag(nationalNum, FLAG_GET_CAUGHT) ? "true" : "false");
    DecodeGameString(text, sizeof(text), GetSpeciesCategory(species), 13);
    JsonPut(writer, ",\"category\":");
    JsonPutString(writer, text);
    JsonPut(writer, ",\"height\":%u,\"weight\":%u", (unsigned)GetSpeciesHeight(species), (unsigned)GetSpeciesWeight(species));
    DecodeGameText(text, sizeof(text), GetSpeciesPokedexDescription(species), 400);
    JsonPut(writer, ",\"text\":");
    JsonPutString(writer, text);
    JsonPut(writer, ",\"types\":[%u,%u],\"base\":[%u,%u,%u,%u,%u,%u]",
            GetSpeciesType(species, 0), GetSpeciesType(species, 1),
            info->baseHP, info->baseAttack, info->baseDefense, info->baseSpAttack, info->baseSpDefense, info->baseSpeed);

    JsonPut(writer, ",\"abilities\":[");
    first = TRUE;
    for (i = 0; i < NUM_ABILITY_SLOTS; i++)
    {
        u32 ability = GetSpeciesAbility(species, i);

        if (ability == ABILITY_NONE || ability >= ABILITIES_COUNT)
            continue;
        DecodeGameString(text, sizeof(text), gAbilitiesInfo[ability].name, ABILITY_NAME_LENGTH);
        JsonPut(writer, first ? "{\"name\":" : ",{\"name\":");
        JsonPutString(writer, text);
        JsonPut(writer, ",\"hidden\":%s}", i == NUM_ABILITY_SLOTS - 1 ? "true" : "false");
        first = FALSE;
    }
    JsonPut(writer, "]");

    DexEvolutionsToJson(writer, species);
    DexWhereToJson(writer, species);

    JsonPut(writer, ",\"learnset\":[");
    first = TRUE;
    learnset = GetSpeciesLevelUpLearnset(species);
    for (i = 0; learnset != NULL && learnset[i].move != LEVEL_UP_MOVE_END && i < 64; i++)
    {
        u32 move = learnset[i].move;

        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        DecodeGameString(text, sizeof(text), GetMoveName(move), MOVE_NAME_LENGTH);
        JsonPut(writer, "%s{\"lv\":%u,\"move\":", first ? "" : ",", learnset[i].level);
        first = FALSE;
        JsonPutString(writer, text);
        JsonPut(writer, ",\"typeId\":%u}", GetMoveType(move));
    }
    JsonPut(writer, "]");
}

// The species' front picture, first frame, 64x64.
static bool32 DexEntryImage(u32 species, u32 *argb, u32 *width, u32 *height)
{
    static u8 sPic[MON_PIC_SIZE * 4];

    if (species == SPECIES_NONE || species >= NUM_SPECIES || !IsSpeciesEnabled(species)
     || gSpeciesInfo[species].frontPic == NULL
     || gSpeciesInfo[species].palette == NULL)
        return FALSE;
    LoadSpecialPokePic(sPic, species, 0, TRUE);
    TilesToArgb(sPic, gSpeciesInfo[species].palette, 8, 8, argb);
    *width = 64;
    *height = 64;
    return TRUE;
}

// --- Region map ---

#define DS_MAP_COLUMNS 28  // MAP_WIDTH in src/region_map.c
#define DS_MAP_ROWS 15     // MAP_HEIGHT
#define DS_MAP_LEFT 1      // MAPCURSOR_X_MIN: the grid's first column, in tiles
#define DS_MAP_TOP 2       // MAPCURSOR_Y_MIN
#define DS_MAP_TILES_WIDE 32
#define DS_MAP_TILES_HIGH 20

static u32 RegionMapType(void)
{
    return GetRegionMapType(gMapHeader.regionMapSectionId);
}

// Where the player is on the grid, as region_map.c's
// InitMapBasedOnPlayerLocation works it out, simplified: the section of the
// map they are on, or for one off the map (an indoor or dungeon map) the
// section of the map they would escape to; scaled by their place in it.
static void MapPlayerPosition(u32 *column, u32 *row, u16 *playerMapSec)
{
    u16 mapSec = gMapHeader.regionMapSectionId;
    u32 x0 = DS_MAP_COLUMNS, y0 = DS_MAP_ROWS, x1 = 0, y1 = 0;
    u32 x, y;
    bool32 found = FALSE;
    const struct MapLayout *layout = gMapHeader.mapLayout;
    s32 posX = gSaveBlock1Ptr->pos.x;
    s32 posY = gSaveBlock1Ptr->pos.y;

    for (y = 0; y < DS_MAP_ROWS && !found; y++)
        for (x = 0; x < DS_MAP_COLUMNS && !found; x++)
            found = RegionMap_MapSecAt(x + DS_MAP_LEFT, y + DS_MAP_TOP) == mapSec;
    if (!found)
    {
        const struct MapHeader *escape = Overworld_GetMapHeaderByGroupAndId(gSaveBlock1Ptr->escapeWarp.mapGroup,
                                                                           gSaveBlock1Ptr->escapeWarp.mapNum);

        if (escape != NULL)
        {
            mapSec = escape->regionMapSectionId;
            layout = escape->mapLayout;
            posX = gSaveBlock1Ptr->escapeWarp.x;
            posY = gSaveBlock1Ptr->escapeWarp.y;
        }
    }
    *playerMapSec = mapSec;

    for (y = 0; y < DS_MAP_ROWS; y++)
    {
        for (x = 0; x < DS_MAP_COLUMNS; x++)
        {
            if (RegionMap_MapSecAt(x + DS_MAP_LEFT, y + DS_MAP_TOP) != mapSec)
                continue;
            x0 = min(x0, x);
            y0 = min(y0, y);
            x1 = max(x1, x);
            y1 = max(y1, y);
        }
    }
    if (x0 > x1)
    {
        *column = DS_MAP_COLUMNS;
        *row = DS_MAP_ROWS;
        return;
    }
    *column = x0;
    *row = y0;
    if (layout != NULL && layout->width > 0 && layout->height > 0)
    {
        if (posX > 0)
            *column += min((u32)posX * (x1 - x0 + 1) / layout->width, x1 - x0);
        if (posY > 0)
            *row += min((u32)posY * (y1 - y0 + 1) / layout->height, y1 - y0);
    }
}

// The grid of section ids (rows of columns), the names of the sections on
// it, and where the player is.
static void MapToJson(struct JsonWriter *writer)
{
    static bool8 sListed[MAPSEC_NONE + 1];
    u8 mapName[64];
    char text[DS_TEXT_LENGTH];
    u32 x, y, column, row;
    u16 playerMapSec;
    bool32 first = TRUE;

    MapPlayerPosition(&column, &row, &playerMapSec);
    JsonPut(writer, ",\"region\":%u,\"columns\":%u,\"rows\":%u,\"left\":%u,\"top\":%u,\"player\":[%u,%u]",
            RegionMapType(), DS_MAP_COLUMNS, DS_MAP_ROWS, DS_MAP_LEFT, DS_MAP_TOP, column, row);
    JsonPut(writer, ",\"grid\":[");
    for (y = 0; y < DS_MAP_ROWS; y++)
    {
        for (x = 0; x < DS_MAP_COLUMNS; x++)
            JsonPut(writer, "%s%u", y == 0 && x == 0 ? "" : ",", RegionMap_MapSecAt(x + DS_MAP_LEFT, y + DS_MAP_TOP));
    }
    JsonPut(writer, "],\"names\":{");
    memset(sListed, 0, sizeof(sListed));
    for (y = 0; y < DS_MAP_ROWS; y++)
    {
        for (x = 0; x < DS_MAP_COLUMNS; x++)
        {
            u32 mapSec = RegionMap_MapSecAt(x + DS_MAP_LEFT, y + DS_MAP_TOP);

            if (mapSec >= MAPSEC_NONE || sListed[mapSec])
                continue;
            sListed[mapSec] = TRUE;
            mapName[0] = EOS;
            GetMapName(mapName, mapSec, 0);
            DecodeGameString(text, sizeof(text), mapName, sizeof(mapName) - 1);
            JsonPut(writer, "%s\"%u\":", first ? "" : ",", mapSec);
            JsonPutString(writer, text);
            first = FALSE;
        }
    }
    JsonPut(writer, "}");
}

// The region map as the map screen draws it: an 8bpp affine background whose
// pixel values index the BG palette from colour 112 (BG_PLTT_ID(7)).
static bool32 MapImage(u32 *argb, u32 *width, u32 *height)
{
    static u8 sTiles[256 * 64];
    static u8 sTilemap[64 * 64];
    const struct RegionMapInfo *info = &gRegionMapInfos[RegionMapType()];
    u32 x, y;

    if (info->regionMapGfx == NULL || info->regionMapTilemap == NULL || info->regionMapPalette == NULL)
        return FALSE;
    DecompressDataWithHeaderWram(info->regionMapGfx, sTiles);
    DecompressDataWithHeaderWram(info->regionMapTilemap, sTilemap);
    for (y = 0; y < DS_MAP_TILES_HIGH * 8; y++)
    {
        for (x = 0; x < DS_MAP_TILES_WIDE * 8; x++)
        {
            u8 tile = sTilemap[(y / 8) * 64 + (x / 8)];
            u8 value = sTiles[tile * 64 + (y % 8) * 8 + (x % 8)];

            argb[y * DS_MAP_TILES_WIDE * 8 + x] = value >= 112 && value < 112 + 32
                                                 ? Rgb555ToArgb(info->regionMapPalette[value - 112]) : 0xFF000000;
        }
    }
    *width = DS_MAP_TILES_WIDE * 8;
    *height = DS_MAP_TILES_HIGH * 8;
    return TRUE;
}

// What decides a document's picture: it is drawn again only when this changes.
static u32 DocImageKey(u32 kind, u32 param)
{
    if (kind == DS_DOC_MAP)
        return (kind << 24) | RegionMapType();
    return (kind << 24) | (param & 0xFFFFFF);
}

// --- Publishing ---

static u32 DocInterval(u32 kind)
{
    switch (kind)
    {
    case DS_DOC_BAG:       return 30;
    case DS_DOC_DEX_ENTRY: return 240;
    default:               return 60;
    }
}

// Builds the document's JSON body after its header. Returns FALSE for a
// kind it does not know.
static bool32 BuildDoc(struct JsonWriter *writer, u32 kind, u32 param)
{
    switch (kind)
    {
    case DS_DOC_BAG:
        BagToJson(writer);
        return TRUE;
    case DS_DOC_GUIDE:
        GuideToJson(writer);
        return TRUE;
    case DS_DOC_DEX:
        DexToJson(writer);
        return TRUE;
    case DS_DOC_DEX_ENTRY:
        DexEntryToJson(writer, param);
        return TRUE;
    case DS_DOC_MAP:
        MapToJson(writer);
        return TRUE;
    default:
        return FALSE;
    }
}

// Draws the document's picture into sWorkImage. Returns FALSE if it has none.
static bool32 BuildDocImage(u32 kind, u32 param, u32 *width, u32 *height)
{
    switch (kind)
    {
    case DS_DOC_DEX_ENTRY:
        return DexEntryImage(param, sWorkImage, width, height);
    case DS_DOC_MAP:
        return MapImage(sWorkImage, width, height);
    default:
        return FALSE;
    }
}

static void PublishDoc(u32 kind, u32 param, bool32 isNew)
{
    struct JsonWriter writer = { sWorkDoc, 0, sizeof(sWorkDoc), FALSE };
    u32 width = 0, height = 0;
    u32 imageKey = DocImageKey(kind, param);
    bool32 newImage = isNew || imageKey != sDocImageKey;
    bool32 hasImage = FALSE;

    JsonPut(&writer, "{\"doc\":%u,\"param\":%u,\"frame\":%u", kind, param, (unsigned)sFrame);
    if (!BuildDoc(&writer, kind, param))
        return;
    JsonPut(&writer, "}");
    if (writer.overflowed)
    {
        writer.length = snprintf(sWorkDoc, sizeof(sWorkDoc), "{\"doc\":%u,\"param\":%u,\"frame\":%u,\"error\":\"overflow\"}",
                                 kind, param, (unsigned)sFrame);
        if (writer.length >= (int)sizeof(sWorkDoc))
            writer.length = 0;
        sWorkDoc[writer.length] = '\0';
    }
    if (newImage)
    {
        hasImage = BuildDocImage(kind, param, &width, &height) && width * height <= DS_DOC_IMAGE_MAX;
        sDocImageKey = imageKey;
    }

    pthread_mutex_lock(&sLock);
    memcpy(sPublishedDoc, sWorkDoc, writer.length + 1);
    if (newImage)
    {
        sPublishedImageWidth = hasImage ? width : 0;
        sPublishedImageHeight = hasImage ? height : 0;
        if (hasImage)
            memcpy(sPublishedImage, sWorkImage, width * height * sizeof(u32));
    }
    pthread_mutex_unlock(&sLock);
}

static void UpdateDoc(void)
{
    u32 kind, param;
    bool32 isNew;

    pthread_mutex_lock(&sLock);
    kind = sWantedDoc;
    param = sWantedParam;
    pthread_mutex_unlock(&sLock);
    if (kind == DS_DOC_NONE || !sSeenOverworld || gSaveBlock1Ptr == NULL || gSaveBlock2Ptr == NULL)
        return;
    isNew = kind != sDocKind || param != sDocParam;
    if (isNew || sFrame % DocInterval(kind) == 0)
    {
        sDocKind = kind;
        sDocParam = param;
        PublishDoc(kind, param, isNew);
    }
}

void DualScreen_SetWantedDoc(u32 kind, u32 param)
{
    pthread_mutex_lock(&sLock);
    sWantedDoc = kind < DS_DOC_COUNT ? kind : DS_DOC_NONE;
    sWantedParam = param;
    pthread_mutex_unlock(&sLock);
}

int DualScreen_CopyDocJson(char *dest, int capacity)
{
    int length;

    if (capacity <= 0)
        return 0;
    pthread_mutex_lock(&sLock);
    length = strlen(sPublishedDoc);
    if (length > capacity - 1)
        length = 0;
    memcpy(dest, sPublishedDoc, length);
    pthread_mutex_unlock(&sLock);
    dest[length] = '\0';
    return length;
}

int DualScreen_CopyDocImage(u32 *argb, int capacity, u32 *width, u32 *height)
{
    int count;

    pthread_mutex_lock(&sLock);
    count = sPublishedImageWidth * sPublishedImageHeight;
    if (count > capacity)
        count = 0;
    *width = count > 0 ? sPublishedImageWidth : 0;
    *height = count > 0 ? sPublishedImageHeight : 0;
    if (count > 0)
        memcpy(argb, sPublishedImage, count * sizeof(u32));
    pthread_mutex_unlock(&sLock);
    return count;
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
    UpdateDoc();
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
        // HNS_DOC_DUMP=1:0,5:0,4:155 prints those documents (kind:param).
        if (getenv("HNS_DOC_DUMP") != NULL)
        {
            const char *list = getenv("HNS_DOC_DUMP");

            while (*list != '\0')
            {
                char *end;
                u32 kind = strtoul(list, &end, 10);
                u32 param = *end == ':' ? strtoul(end + 1, &end, 10) : 0;

                PublishDoc(kind, param, TRUE);
                fprintf(stderr, "doc %lu %s\n", frame, sPublishedDoc);
                if (sPublishedImageWidth != 0)
                {
                    // The picture as doc_KIND_PARAM.ppm next to the shots.
                    char name[64];
                    FILE *file;
                    u32 i;

                    snprintf(name, sizeof(name), "doc_%u_%u.ppm", kind, param);
                    file = fopen(name, "wb");
                    if (file != NULL)
                    {
                        fprintf(file, "P6\n%u %u\n255\n", sPublishedImageWidth, sPublishedImageHeight);
                        for (i = 0; i < sPublishedImageWidth * sPublishedImageHeight; i++)
                        {
                            u8 rgb[3] = { sPublishedImage[i] >> 16, sPublishedImage[i] >> 8, sPublishedImage[i] };

                            fwrite(rgb, 1, 3, file);
                        }
                        fclose(file);
                    }
                }
                list = *end == ',' ? end + 1 : end;
                if (*end != ',')
                    break;
            }
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

JNIEXPORT void JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeSetWantedDoc(JNIEnv *env, jclass clazz, jint kind, jint param)
{
    (void)env;
    (void)clazz;
    DualScreen_SetWantedDoc(kind >= 0 ? (u32)kind : DS_DOC_NONE, (u32)param);
}

JNIEXPORT jstring JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeGetDocJson(JNIEnv *env, jclass clazz)
{
    static char sJniDoc[DS_DOC_JSON_CAPACITY]; // only the UI thread calls this

    (void)clazz;
    DualScreen_CopyDocJson(sJniDoc, sizeof(sJniDoc));
    return (*env)->NewStringUTF(env, sJniDoc);
}

// The picture as width, height, then the ARGB pixels; null if there is none.
JNIEXPORT jintArray JNICALL Java_com_heartsoul_recomp_DualScreenBridge_nativeGetDocImage(JNIEnv *env, jclass clazz)
{
    static u32 sJniImage[2 + DS_DOC_IMAGE_MAX]; // only the UI thread calls this
    u32 width, height;
    int count;
    jintArray result;

    (void)clazz;
    count = DualScreen_CopyDocImage(sJniImage + 2, DS_DOC_IMAGE_MAX, &width, &height);
    if (count == 0)
        return NULL;
    sJniImage[0] = width;
    sJniImage[1] = height;
    result = (*env)->NewIntArray(env, count + 2);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, count + 2, (const jint *)sJniImage);
    return result;
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
