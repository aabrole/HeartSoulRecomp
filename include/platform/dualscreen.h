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

struct DualScreenMove
{
    u16 id;
    u8 pp;
    u8 maxPp;
    u8 type;
    char name[DS_TEXT_LENGTH];
    char typeName[DS_TEXT_LENGTH];
};

struct DualScreenMon
{
    u16 species;
    bool8 isEgg;
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
    struct DualScreenMon foe;       // name, level, HP and status only
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
void DualScreen_InjectKeys(u16 keys, u32 frames);
void DualScreen_RequestTap(u32 kind, u32 index);

// Implemented in battle_controller_player.c, which owns the input handlers.
// Returns an enum DualScreenBattleMenu and the battler that is choosing.
u32 DualScreen_GetPlayerMenu(u32 *battler);

#endif // PORTABLE

#endif // GUARD_PLATFORM_DUALSCREEN_H
