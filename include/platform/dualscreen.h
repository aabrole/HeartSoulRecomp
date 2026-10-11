#ifndef GUARD_PLATFORM_DUALSCREEN_H
#define GUARD_PLATFORM_DUALSCREEN_H

#ifdef PORTABLE

// Bottom-screen companion bridge. The game thread copies the state the
// second screen shows into a plain snapshot once every few frames and
// publishes it as JSON. Another thread (the Android UI thread) reads the
// published JSON and posts touch requests, which the game thread turns into
// ordinary GBA button presses.

#define DS_TEXT_LENGTH 64 // UTF-8, enough for 20 game characters

enum DualScreenBattleMenu
{
    DS_MENU_NONE,   // not the player's turn to choose
    DS_MENU_ACTION, // FIGHT / BAG / POKEMON / RUN
    DS_MENU_MOVE,   // the four moves
    DS_MENU_TARGET, // picking a target in a double battle
};

// Slots of the action menu, in the game's cursor order.
enum DualScreenAction
{
    DS_ACTION_FIGHT,
    DS_ACTION_BAG,
    DS_ACTION_POKEMON,
    DS_ACTION_RUN,
};

// What a touch on the bottom screen asks for.
enum DualScreenTap
{
    DS_TAP_NONE,
    DS_TAP_MOVE,   // index is the move slot, 0 to 3
    DS_TAP_ACTION, // index is an enum DualScreenAction
};

enum DualScreenStat
{
    DS_STAT_HP,
    DS_STAT_ATK,
    DS_STAT_DEF,
    DS_STAT_SPATK,
    DS_STAT_SPDEF,
    DS_STAT_SPEED,
    DS_STAT_COUNT,
};

struct DualScreenMove
{
    u16 id;
    u8 pp;
    u8 maxPp;
    u8 type;
    u8 category; // enum DamageCategory: physical, special, status
    u8 power;    // 0 for a status move; 1 for one whose power varies
    u8 accuracy; // 0 for a move that never misses
    char name[DS_TEXT_LENGTH];
    char typeName[DS_TEXT_LENGTH];
};

struct DualScreenMon
{
    u16 species;     // SPECIES_EGG for an egg
    u16 iconSpecies; // the species inside an egg too, for its icon
    u32 personality;
    bool8 isEgg;
    bool8 isShiny;
    u8 gender;       // MON_MALE, MON_FEMALE or MON_GENDERLESS
    u8 types[3];     // TYPE_NONE where unused; battle mons can have a third
    bool8 hasDetails; // party mons: the fields below are filled
    char abilityName[DS_TEXT_LENGTH];
    char natureName[DS_TEXT_LENGTH];
    u8 natureUp;     // enum DualScreenStat raised by the nature, or DS_STAT_COUNT
    u8 natureDown;
    u16 stats[DS_STAT_COUNT]; // stats[DS_STAT_HP] is the max HP
    u8 ivs[DS_STAT_COUNT];
    u8 evs[DS_STAT_COUNT];
    u32 expThisLevel;  // experience gained since the current level
    u32 expNextLevel;  // experience the current level takes, 0 at the max level
    u8 friendship;
    u8 level;
    u16 hp;
    u16 maxHp;
    char speciesName[DS_TEXT_LENGTH];
    char nickname[DS_TEXT_LENGTH];
    char status[4]; // "", SLP, PSN, TOX, BRN, FRZ, FRB, PAR or FNT
    char itemName[DS_TEXT_LENGTH];
    u8 moveCount;
    struct DualScreenMove moves[MAX_MON_MOVES];
};

struct DualScreenBattle
{
    bool8 active;
    bool8 isDouble;
    bool8 isTrainer;
    u8 menu;        // enum DualScreenBattleMenu
    u8 actionCursor;
    u8 moveCursor;
    u8 partyIndex;  // party slot of the mon that is choosing, or of the left mon
    bool8 hasPlayerMon;
    bool8 hasFoe;
    struct DualScreenMon playerMon; // battle values: Transform, Mimic and in-battle PP
    struct DualScreenMon foe;       // name, level, HP, status and types only
    u8 foeTeamCount;  // trainer battles: the opponents' Pokémon, eggs left out
    u8 foeTeamLeft;   // of those, the ones not fainted
};

struct DualScreenSnapshot
{
    u32 frame;
    bool8 inGame;    // a save was continued or a new game was started
    bool8 overworld; // the field is the running screen right now
    char playerName[DS_TEXT_LENGTH];
    char mapName[DS_TEXT_LENGTH];
    u32 money;
    u8 badges;
    u16 playHours;
    u8 playMinutes;
    u8 playSeconds;
    u8 partyCount;
    struct DualScreenMon party[PARTY_SIZE];
    struct DualScreenBattle battle;
    bool8 fastForward;
};

// Game thread only: reads live game state.
void DualScreen_FillSnapshot(struct DualScreenSnapshot *snapshot);
// Writes the snapshot as compact JSON. Returns the length, without the NUL.
int DualScreen_SnapshotToJson(const struct DualScreenSnapshot *snapshot, char *dest, int capacity);

// Game thread, once a frame after the game logic has run: drives a pending
// touch request and publishes a fresh snapshot every few frames.
void DualScreen_FrameHook(void);
// Headless test mode, called instead of DualScreen_FrameHook. Handles
// HNS_STATE_DUMP and HNS_TAP (see src/platform/sdl2.c).
void DualScreen_HeadlessFrame(unsigned long frame);
// Game thread, once a frame from Platform_GetKeyInput: the buttons injected
// for this frame. Each call uses up one frame of the queue.
u16 DualScreen_ConsumeInjectedKeys(void);

// Any thread.
int DualScreen_CopyJson(char *dest, int capacity);
// The bag as JSON, published every DS_BAG_INTERVAL frames while wanted. Before
// the first publish after it is wanted, an empty bag.
void DualScreen_SetBagWanted(bool32 wanted);
int DualScreen_CopyBagJson(char *dest, int capacity);
// The type chart as JSON: type names and every attack-on-defence multiplier.
// Reads constant data only.
int DualScreen_TypeChartJson(char *dest, int capacity);
// A Pokémon's menu icon (first frame) as 32x32 ARGB, 0 where transparent.
// Reads constant data only. Returns FALSE for a species out of range.
bool32 DualScreen_MonIcon(u32 species, u32 personality, bool32 isEgg, u32 *argb);
// An item's bag icon as 24x24 ARGB. The icons are compressed and only the
// game thread may decompress, so the first ask for an item returns FALSE and
// queues it; the game thread decodes it within a few frames.
bool32 DualScreen_ItemIcon(u32 item, u32 *argb);
void DualScreen_InjectKeys(u16 keys, u32 frames);
void DualScreen_RequestTap(u32 kind, u32 index);

// Implemented in battle_controller_player.c, which owns the input handlers.
// Returns an enum DualScreenBattleMenu and the battler that is choosing.
u32 DualScreen_GetPlayerMenu(u32 *battler);

#endif // PORTABLE

#endif // GUARD_PLATFORM_DUALSCREEN_H
