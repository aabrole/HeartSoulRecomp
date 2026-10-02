#ifdef PLATFORM_SDL2
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#endif
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

#ifdef __ANDROID__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include "global.h"
#include "platform.h"
#include "main.h"
#include "battle_setup.h"
#include "script_pokemon_util.h"
#include "constants/species.h"
#include "constants/items.h"
#include "constants/flags.h"
#include "event_data.h"
#include "rtc.h"
#include "gba/defines.h"
#include "gba/m4a_internal.h"
#include "m4a.h"
#include "cgb_audio.h"
#include "gba/flash_internal.h"
#include "platform/dma.h"
#include "platform/framedraw.h"
#include "platform/system.h"
#include "platform/dualscreen.h"


SDL_Thread *mainLoopThread;
SDL_Window *sdlWindow;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;
SDL_sem *vBlankSemaphore;
SDL_atomic_t isFrameAvailable;
bool speedUp = false;
unsigned int videoScale = 1;
bool videoScaleChanged = false;
bool isRunning = true;
bool paused = false;
double simTime = 0;
double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;
struct SiiRtcInfo internalClock;

static FILE *sSaveFile = NULL;

extern void AgbMain(void);
extern void MainLoop(void);
extern void DoSoftReset(void);

int DoMain(void *param);
void ProcessEvents(void);
void VDraw(SDL_Texture *texture);

static void ReadSaveFile(char *path);
static void StoreSaveFile(void);
static void CloseSaveFile(void);

static void UpdateInternalClock(void);

static u16 keys;
static uint16_t sFrameImage[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static bool sHeadless = false;

// Headless test mode, for running the game without a display or a person.
//   HNS_HEADLESS_FRAMES=N   run N frames as fast as possible, then exit
//   HNS_SHOTS=60,300        save shot_00060.bmp and shot_00300.bmp
//   HNS_SHOT_EVERY=N        also save a shot every N frames
//   HNS_INPUT=120:A,200+30:DOWN   press A on frame 120, hold DOWN for 30 frames from 200
//   HNS_STATE_DUMP=N        print the bottom-screen state JSON to stderr every N frames
//   HNS_TAP=2700:MOVE1,2900:RUN   bottom-screen taps: MOVE1-4, FIGHT, BAG, POKEMON, RUN
//   HNS_WAV=path            write everything the game queues as audio to a WAV file
//                           (32-bit float, stereo, 42048 Hz)
//   HNS_AUDIO_LOG=1         log song starts, unhandled sound commands and voice
//                           types to stderr, and print note counts at the end
//   HNS_GBS=FRAME           set FLAG_SYS_GBS_ENABLED on that frame, so music started
//                           afterwards uses the Game Boy sound engine
//   HNS_SONG=300:5,400:21   start song 5 on frame 300 and song 21 on frame 400, to hear
//                           a sound effect or a piece of music on its own
// Use with SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy.
static u16 HeadlessKeyFromName(const char *name, size_t len)
{
    static const struct { const char *name; u16 key; } names[] = {
        {"A", A_BUTTON}, {"B", B_BUTTON}, {"START", START_BUTTON}, {"SELECT", SELECT_BUTTON},
        {"L", L_BUTTON}, {"R", R_BUTTON}, {"UP", DPAD_UP}, {"DOWN", DPAD_DOWN},
        {"LEFT", DPAD_LEFT}, {"RIGHT", DPAD_RIGHT},
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        if (strlen(names[i].name) == len && strncmp(names[i].name, name, len) == 0)
            return names[i].key;
    }
    return 0;
}

static u16 HeadlessKeysForFrame(const char *script, unsigned long frame)
{
    u16 result = 0;

    while (script != NULL && *script != '\0')
    {
        char *end;
        unsigned long start = strtoul(script, &end, 10);
        unsigned long length = 2;
        const char *name;
        size_t nameLen;

        if (*end == '+')
            length = strtoul(end + 1, &end, 10);
        if (*end != ':')
            break;
        name = end + 1;
        nameLen = strcspn(name, ",");
        if (frame >= start && frame < start + length)
            result |= HeadlessKeyFromName(name, nameLen);
        script = name + nameLen;
        if (*script == ',')
            script++;
    }
    return result;
}

static bool HeadlessWantsShot(const char *list, unsigned long every, unsigned long frame)
{
    if (every != 0 && frame % every == 0)
        return true;
    while (list != NULL && *list != '\0')
    {
        char *end;

        if (strtoul(list, &end, 10) == frame)
            return true;
        if (*end != ',')
            break;
        list = end + 1;
    }
    return false;
}

// Starts the songs HNS_SONG lists for this frame.
static void HeadlessStartSongs(const char *list, unsigned long frame, bool32 gbsEnabled)
{
    while (list != NULL && *list != '\0')
    {
        char *end;
        unsigned long start = strtoul(list, &end, 10);
        unsigned long songNum;

        if (*end != ':')
            break;
        songNum = strtoul(end + 1, &end, 10);
        if (start == frame)
            m4aSongNumStart(songNum, gbsEnabled);
        if (*end != ',')
            break;
        list = end + 1;
    }
}

static void HeadlessSaveShot(unsigned long frame)
{
    char path[64];
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(sFrameImage, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                                              16, DISPLAY_WIDTH * sizeof(uint16_t), SDL_PIXELFORMAT_ABGR1555);

    if (surface == NULL)
        return;
    snprintf(path, sizeof(path), "shot_%05lu.bmp", frame);
    // 24-bit, since few image tools read 16-bit bitmaps.
    {
        SDL_Surface *rgb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_BGR24, 0);

        if (rgb != NULL)
        {
            SDL_SaveBMP(rgb, path);
            SDL_FreeSurface(rgb);
        }
    }
    SDL_FreeSurface(surface);
}

#define WAV_SAMPLE_RATE 42048
#define WAV_HEADER_SIZE 44

static FILE *sWavFile = NULL;
static u32 sWavDataBytes = 0;
static u32 sWavWrites = 0;

static void WavPut32(u8 *dest, u32 value)
{
    dest[0] = value;
    dest[1] = value >> 8;
    dest[2] = value >> 16;
    dest[3] = value >> 24;
}

// Rewritten as the file grows, so a run that is killed still leaves a valid file.
static void WavWriteHeader(void)
{
    u8 header[WAV_HEADER_SIZE];

    memcpy(header, "RIFF", 4);
    WavPut32(header + 4, WAV_HEADER_SIZE - 8 + sWavDataBytes);
    memcpy(header + 8, "WAVEfmt ", 8);
    WavPut32(header + 16, 16);
    header[20] = 3; // IEEE float
    header[21] = 0;
    header[22] = 2; // channels
    header[23] = 0;
    WavPut32(header + 24, WAV_SAMPLE_RATE);
    WavPut32(header + 28, WAV_SAMPLE_RATE * 2 * sizeof(float));
    header[32] = 2 * sizeof(float);
    header[33] = 0;
    header[34] = 8 * sizeof(float);
    header[35] = 0;
    memcpy(header + 36, "data", 4);
    WavPut32(header + 40, sWavDataBytes);
    fseek(sWavFile, 0, SEEK_SET);
    fwrite(header, 1, sizeof(header), sWavFile);
    fseek(sWavFile, 0, SEEK_END);
    fflush(sWavFile);
}

static void WavOpen(const char *path)
{
    sWavFile = fopen(path, "wb");
    if (sWavFile == NULL)
    {
        fprintf(stderr, "headless: cannot write %s\n", path);
        return;
    }
    sWavDataBytes = 0;
    sWavWrites = 0;
    WavWriteHeader();
}

static void WavWrite(const float *samples, u32 size)
{
    if (sWavFile == NULL)
        return;
    fwrite(samples, 1, size, sWavFile);
    sWavDataBytes += size;
    // About once a second.
    if (++sWavWrites % 60 == 0)
        WavWriteHeader();
}

static void WavClose(void)
{
    if (sWavFile == NULL)
        return;
    WavWriteHeader();
    fclose(sWavFile);
    sWavFile = NULL;
}

#ifndef _WIN32
// qemu's debugger stub only reports faults, so the watchdog turns a stuck
// frame into one. "thread apply all bt" then shows where the game was.
static void HeadlessWatchdog(int signum, siginfo_t *info, void *context)
{
    (void)signum;
    (void)info;
    fprintf(stderr, "headless: frame did not finish, callback2=%p state=%d\n", (void *)gMain.callback2, gMain.state);
#if defined(__arm__) && defined(__linux__)
    {
        ucontext_t *uc = context;

        fprintf(stderr, "headless: stuck at pc=%#lx lr=%#lx\n", uc->uc_mcontext.arm_pc, uc->uc_mcontext.arm_lr);
    }
#else
    (void)context;
#endif
    *(volatile int *)0 = 0;
}
#endif

static int RunHeadless(unsigned long frameCount)
{
    const char *script = getenv("HNS_INPUT");
    const char *shots = getenv("HNS_SHOTS");
    const char *everyText = getenv("HNS_SHOT_EVERY");
    unsigned long every = everyText != NULL ? strtoul(everyText, NULL, 10) : 0;
    const char *testBattleText = getenv("HNS_TEST_BATTLE");
    unsigned long testBattleFrame = testBattleText != NULL ? strtoul(testBattleText, NULL, 10) : 0;
    const char *gbsText = getenv("HNS_GBS");
    unsigned long gbsFrame = gbsText != NULL ? strtoul(gbsText, NULL, 10) : 0;
    const char *wavPath = getenv("HNS_WAV");
    const char *songs = getenv("HNS_SONG");
    unsigned long frame;

    if (wavPath != NULL && wavPath[0] != '\0')
        WavOpen(wavPath);

#ifndef _WIN32
    {
        struct sigaction action;

        memset(&action, 0, sizeof(action));
        action.sa_sigaction = HeadlessWatchdog;
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGALRM, &action, NULL);
    }
#endif
    for (frame = 1; frame <= frameCount; frame++)
    {
        keys = HeadlessKeysForFrame(script, frame);
        // HNS_TEST_BATTLE=FRAME gives the player a Cyndaquil and starts a wild
        // battle on that frame. The player must be standing in the overworld.
        if (testBattleFrame != 0 && frame == testBattleFrame)
        {
            ScriptGiveMon(SPECIES_CYNDAQUIL, 10, ITEM_NONE);
            CreateScriptedWildMon(SPECIES_SENTRET, 3, ITEM_NONE);
            BattleSetup_StartScriptedWildBattle();
        }
        if (gbsFrame != 0 && frame == gbsFrame)
            FlagSet(FLAG_SYS_GBS_ENABLED);
        gAudioLogFrame = frame;
        HeadlessStartSongs(songs, frame, gbsFrame != 0 && frame >= gbsFrame);
#ifndef _WIN32
        // A frame that never finishes ends the run with SIGALRM, which a
        // debugger reports with the place it was stuck.
        alarm(20);
#endif
        ENTER_VBLANK();
        MainLoop();
        DualScreen_HeadlessFrame(frame);
        VDraw(sdlTexture);
        RunDMAsAndVBlank();
        AudioUpdate();
        if (HeadlessWantsShot(shots, every, frame))
            HeadlessSaveShot(frame);
    }
    WavClose();
    if (gAudioLog)
        AudioLogSummary();
    printf("headless: ran %lu frames\n", frameCount);
    return 0;
}

int main(int argc, char **argv)
{
    const char *headlessFrames = getenv("HNS_HEADLESS_FRAMES");

    // Open an output console on Windows
#ifdef _WIN32
    AllocConsole() ;
    AttachConsole( GetCurrentProcessId() ) ;
    freopen( "CON", "w", stdout ) ;
#endif

#ifdef __ANDROID__
    // The save lives in the app's own external files folder
    // (Android/data/<package>/files), which needs no permission and which a
    // file manager can reach to back the save up.
    {
        const char *dir = SDL_AndroidGetExternalStoragePath();

        if (dir != NULL)
            chdir(dir);
    }
#endif

    ReadSaveFile("pokeemerald.sav");

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        DBGPRINTF("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    sdlWindow = SDL_CreateWindow("pokeemerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale,
#ifdef __ANDROID__
                                 SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
#else
                                 SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif
    if (sdlWindow == NULL)
    {
        DBGPRINTF("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
    if (sdlRenderer == NULL)
    {
        DBGPRINTF("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, 255);
    SDL_RenderClear(sdlRenderer);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    sdlTexture = SDL_CreateTexture(sdlRenderer,
                                   SDL_PIXELFORMAT_ABGR1555,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (sdlTexture == NULL)
    {
        DBGPRINTF("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    simTime = curGameTime = lastGameTime = SDL_GetPerformanceCounter();

    isFrameAvailable.value = 0;
    vBlankSemaphore = SDL_CreateSemaphore(0);

    SDL_AudioSpec want;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = 42048;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    cgb_audio_init(want.freq);


    if (SDL_OpenAudio(&want, 0) < 0)
        SDL_Log("Failed to open audio: %s", SDL_GetError());
    else
    {
        if (want.format != AUDIO_F32) /* we let this one thing change. */
            SDL_Log("We didn't get Float32 audio format.");
        SDL_PauseAudio(0);
    }

    memset(&internalClock, 0, sizeof(internalClock));
    internalClock.status = SIIRTCINFO_24HOUR;
    UpdateInternalClock();

    sHeadless = headlessFrames != NULL;
    gAudioLog = getenv("HNS_AUDIO_LOG") != NULL;

    AgbMain();

    if (sHeadless)
    {
        int result = RunHeadless(strtoul(headlessFrames, NULL, 10));

        CloseSaveFile();
        SDL_Quit();
        return result;
    }

    double accumulator = 0.0;

    bool isGameStepDrawn = false;
    while (isRunning)
    {
        double deltaTime;

        ProcessEvents();

        curGameTime = SDL_GetPerformanceCounter();
        deltaTime = (double)((curGameTime - lastGameTime) / (double)SDL_GetPerformanceFrequency());
        deltaTime *= timeScale; //apply speedup

        if (!paused)
        {
            accumulator += deltaTime;

            isGameStepDrawn = false;

            while (accumulator >= fixedTimestep)
            {
                //run game logic, draw frame and process DMAs and vblank
                ENTER_VBLANK(); //you must be in VBlank before running a game tick
                MainLoop();
                DualScreen_FrameHook();
                if (!isGameStepDrawn)
                {
                    VDraw(sdlTexture);
                    //SDL_RenderClear(sdlRenderer);
                    isGameStepDrawn = true;
                }
                RunDMAsAndVBlank();

                accumulator -= fixedTimestep;
            }

            //samples per frame is 701, that gets multipled by two when being queued and then multipled by four because samples are float32 which are 4 bytes long hence the divide by 8
            //this number is then checked against samples per frame multipled by three rounded down to 2000 to give it enough margin of error while not desyncing
            //this is all done to sync audio to gameplay
            if (SDL_GetQueuedAudioSize(1)/8 < 2000)
            {
                AudioUpdate();
            }

            if (videoScaleChanged)
            {
                SDL_SetWindowSize(sdlWindow, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale);
                videoScaleChanged = false;
            }
        }

        lastGameTime = curGameTime;

        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
        SDL_RenderPresent(sdlRenderer);
    }

    CloseSaveFile();

    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
    return 0;
}

static void ReadSaveFile(char *path)
{
    // Check whether the saveFile exists, and create it if not
    sSaveFile = fopen(path, "r+b");
    if (sSaveFile == NULL)
    {
        sSaveFile = fopen(path, "w+b");
    }

    fseek(sSaveFile, 0, SEEK_END);
    int fileSize = ftell(sSaveFile);
    fseek(sSaveFile, 0, SEEK_SET);

    // Only read as many bytes as fit inside the buffer
    // or as many bytes as are in the file
    int bytesToRead = (fileSize < sizeof(FLASH_BASE)) ? fileSize : sizeof(FLASH_BASE);

    int bytesRead = fread(FLASH_BASE, 1, bytesToRead, sSaveFile);

    // Fill the buffer if the savefile was just created or smaller than the buffer itself
    for (int i = bytesRead; i < sizeof(FLASH_BASE); i++)
    {
        FLASH_BASE[i] = 0xFF;
    }
}

static void StoreSaveFile()
{
    if (sSaveFile != NULL)
    {
        fseek(sSaveFile, 0, SEEK_SET);
        fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), sSaveFile);
    }
}

void Platform_StoreSaveFile(void)
{
    StoreSaveFile();
}

void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    DBGPRINTF("ReadFlash(sectorNum=0x%04X,offset=0x%08X,size=0x%02X)\n",sectorNum,offset,size);
    FILE * savefile = fopen("pokeemerald.sav", "r+b");
    if (savefile == NULL)
    {
        puts("Error opening save file.");
        return;
    }
    if (fseek(savefile, (sectorNum << gFlash->sector.shift) + offset, SEEK_SET))
    {
        fclose(savefile);
        return;
    }
    if (fread(dest, 1, size, savefile) != size)
    {
        fclose(savefile);
        return;
    }
    fclose(savefile);
}

void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame)
{
    // samplesPerFrame is a size in bytes.
    if (sHeadless)
    {
        WavWrite(audioBuffer, samplesPerFrame);
        return;
    }
    SDL_QueueAudio(1, audioBuffer, samplesPerFrame);
}


static void CloseSaveFile()
{
    if (sSaveFile != NULL)
    {
        fclose(sSaveFile);
    }
}

// Key mappings
#define KEY_A_BUTTON      SDLK_z
#define KEY_B_BUTTON      SDLK_x
#define KEY_START_BUTTON  SDLK_RETURN
#define KEY_SELECT_BUTTON SDLK_BACKSLASH
#define KEY_L_BUTTON      SDLK_a
#define KEY_R_BUTTON      SDLK_s
#define KEY_DPAD_UP       SDLK_UP
#define KEY_DPAD_DOWN     SDLK_DOWN
#define KEY_DPAD_LEFT     SDLK_LEFT
#define KEY_DPAD_RIGHT    SDLK_RIGHT

#define HANDLE_KEYUP(key) \
case KEY_##key:  keys &= ~key; break;

#define HANDLE_KEYDOWN(key) \
case KEY_##key:  keys |= key; break;

// Game controllers. Buttons are mapped by position, as on a GBA: A is the
// right face button and B the bottom one, whatever the pad prints on them.
static u16 sPadKeys;
static bool sPadFastForward;

static u16 PadKeyFromButton(Uint8 button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_B:             return A_BUTTON;
    case SDL_CONTROLLER_BUTTON_A:             return B_BUTTON;
    case SDL_CONTROLLER_BUTTON_START:         return START_BUTTON;
    case SDL_CONTROLLER_BUTTON_BACK:          return SELECT_BUTTON;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return L_BUTTON;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return R_BUTTON;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return DPAD_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return DPAD_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return DPAD_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return DPAD_RIGHT;
    default:                                  return 0;
    }
}

#define PAD_STICK_THRESHOLD 16000

static u16 PadStickKeys(void)
{
    u16 result = 0;
    int i;

    for (i = 0; i < SDL_NumJoysticks(); i++)
    {
        SDL_GameController *pad = SDL_IsGameController(i) ? SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i)) : NULL;
        int x, y;

        if (pad == NULL)
            continue;
        x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (x < -PAD_STICK_THRESHOLD) result |= DPAD_LEFT;
        if (x >  PAD_STICK_THRESHOLD) result |= DPAD_RIGHT;
        if (y < -PAD_STICK_THRESHOLD) result |= DPAD_UP;
        if (y >  PAD_STICK_THRESHOLD) result |= DPAD_DOWN;
    }
    return result;
}

void ProcessEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            isRunning = false;
            break;
        case SDL_CONTROLLERDEVICEADDED:
            SDL_GameControllerOpen(event.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            sPadKeys |= PadKeyFromButton(event.cbutton.button);
            break;
        case SDL_CONTROLLERBUTTONUP:
            sPadKeys &= ~PadKeyFromButton(event.cbutton.button);
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
            {
                // Hold the right trigger to fast forward.
                sPadFastForward = event.caxis.value > PAD_STICK_THRESHOLD;
                if (!speedUp)
                    timeScale = sPadFastForward ? 5.0 : 1.0;
            }
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYUP(A_BUTTON)
            HANDLE_KEYUP(B_BUTTON)
            HANDLE_KEYUP(START_BUTTON)
            HANDLE_KEYUP(SELECT_BUTTON)
            HANDLE_KEYUP(L_BUTTON)
            HANDLE_KEYUP(R_BUTTON)
            HANDLE_KEYUP(DPAD_UP)
            HANDLE_KEYUP(DPAD_DOWN)
            HANDLE_KEYUP(DPAD_LEFT)
            HANDLE_KEYUP(DPAD_RIGHT)
            case SDLK_SPACE:
                if (speedUp)
                {
                    speedUp = false;
                    timeScale = 1.0;
                    //SDL_ClearQueuedAudio(1);
                    //SDL_PauseAudio(0);
                }
                break;
            }
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYDOWN(A_BUTTON)
            HANDLE_KEYDOWN(B_BUTTON)
            HANDLE_KEYDOWN(START_BUTTON)
            HANDLE_KEYDOWN(SELECT_BUTTON)
            HANDLE_KEYDOWN(L_BUTTON)
            HANDLE_KEYDOWN(R_BUTTON)
            HANDLE_KEYDOWN(DPAD_UP)
            HANDLE_KEYDOWN(DPAD_DOWN)
            HANDLE_KEYDOWN(DPAD_LEFT)
            HANDLE_KEYDOWN(DPAD_RIGHT)
            case SDLK_r:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    DoSoftReset();
                }
                break;
            case SDLK_p:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    paused = !paused;
                }
                break;
            case SDLK_SPACE:
                if (!speedUp)
                {
                    speedUp = true;
                    timeScale = 5.0;
                    //SDL_PauseAudio(1);
                }
                break;
            }
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                unsigned int w = event.window.data1;
                unsigned int h = event.window.data2;
                
                videoScale = 0;
                if (w / DISPLAY_WIDTH > videoScale)
                    videoScale = w / DISPLAY_WIDTH;
                if (h / DISPLAY_HEIGHT > videoScale)
                    videoScale = h / DISPLAY_HEIGHT;
                if (videoScale < 1)
                    videoScale = 1;

                videoScaleChanged = true;
            }
            break;
        }
    }
}

#ifdef _WIN32
#define STICK_THRESHOLD 0.5f
u16 GetXInputKeys()
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    DWORD dwResult = XInputGetState(0, &state);
    u16 xinputKeys = 0;

    if (dwResult == ERROR_SUCCESS)
    {
        /* A */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) >> 12;
        /* B */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) >> 13;
        /* Start */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) >> 1;
        /* Select */ xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) >> 3;
        /* L */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) << 1;
        /* R */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) >> 1;
        /* Up */     xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) << 6;
        /* Down */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) << 6;
        /* Left */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) << 3;
        /* Right */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) << 1;


        /* Control Stick */
        float xAxis = (float)state.Gamepad.sThumbLX / (float)SHRT_MAX;
        float yAxis = (float)state.Gamepad.sThumbLY / (float)SHRT_MAX;

        if (xAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_LEFT;
        if (xAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_RIGHT;
        if (yAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_DOWN;
        if (yAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_UP;


        /* Speedup */
        // Note: 'speedup' variable is only (un)set on keyboard input
        double oldTimeScale = timeScale;
        timeScale = (state.Gamepad.bRightTrigger > 0x80 || speedUp) ? 5.0 : 1.0;

        if (oldTimeScale != timeScale)
        {
            if (timeScale > 1.0)
            {
                SDL_PauseAudio(1);
            }
            else
            {
                SDL_ClearQueuedAudio(1);
                SDL_PauseAudio(0);
            }
        }
    }

    return xinputKeys;
}
#endif // _WIN32

u16 Platform_GetKeyInput(void)
{
#ifdef _WIN32
    u16 gamepadKeys = GetXInputKeys();
    return (gamepadKeys != 0) ? gamepadKeys : keys;
#endif

    return keys | sPadKeys | PadStickKeys() | DualScreen_ConsumeInjectedKeys();
}

void VDraw(SDL_Texture *texture)
{
    memset(sFrameImage, 0, sizeof(sFrameImage));
    DrawFrame(sFrameImage);
    SDL_UpdateTexture(texture, NULL, sFrameImage, DISPLAY_WIDTH * sizeof (Uint16));
    REG_VCOUNT = 161; // prep for being in VBlank period
}

int DoMain(void *data)
{
    AgbMain();
}

void VBlankIntrWait(void)
{
    return;
}

u8 BinToBcd(u8 bin)
{
    int placeCounter = 1;
    u8 out = 0;
    do
    {
        out |= (bin % 10) * placeCounter;
        placeCounter *= 16;
    }
    while ((bin /= 10) > 0);

    return out;
}

void Platform_GetStatus(struct SiiRtcInfo *rtc)
{
    rtc->status = internalClock.status;
}

void Platform_SetStatus(struct SiiRtcInfo *rtc)
{
    internalClock.status = rtc->status;
}

static void UpdateInternalClock(void)
{
    time_t rawTime = time(NULL);
    struct tm *time = localtime(&rawTime);

    internalClock.year = BinToBcd(time->tm_year - 100);
    internalClock.month = BinToBcd(time->tm_mon + 1);
    internalClock.day = BinToBcd(time->tm_mday);
    internalClock.dayOfWeek = BinToBcd(time->tm_wday);
    internalClock.hour = BinToBcd(time->tm_hour);
    internalClock.minute = BinToBcd(time->tm_min);
    internalClock.second = BinToBcd(time->tm_sec);
}

void Platform_GetDateTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->year = internalClock.year;
    rtc->month = internalClock.month;
    rtc->day = internalClock.day;
    rtc->dayOfWeek = internalClock.dayOfWeek;
    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    DBGPRINTF("GetDateTime: %d-%02d-%02d %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->year),
                                                         ConvertBcdToBinary(rtc->month),
                                                         ConvertBcdToBinary(rtc->day),
                                                         ConvertBcdToBinary(rtc->hour),
                                                         ConvertBcdToBinary(rtc->minute),
                                                         ConvertBcdToBinary(rtc->second));
}

void Platform_SetDateTime(struct SiiRtcInfo *rtc)
{
    internalClock.month = rtc->month;
    internalClock.day = rtc->day;
    internalClock.dayOfWeek = rtc->dayOfWeek;
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_GetTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    DBGPRINTF("GetTime: %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->hour),
                                        ConvertBcdToBinary(rtc->minute),
                                        ConvertBcdToBinary(rtc->second));
}

void Platform_SetTime(struct SiiRtcInfo *rtc)
{
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_SetAlarm(u8 *alarmData)
{
    // TODO
}

void SoftReset(u32 resetFlags)
{
    puts("Soft Reset called. Exiting.");
    exit(0);
}

#endif