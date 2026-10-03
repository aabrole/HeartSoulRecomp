#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

#include "global.h"
#include "siirtc.h"

// Widescreen rendering geometry, after Goldoire/pokeemerald-dualscreen.
//
// The GBA viewport is DISPLAY_WIDTH (240) wide and every coordinate the game
// computes stays in that space. Widescreen has the software PPU render extra
// columns either side of it, so pixels stay square. Buffers are sized for the
// widest mode; gRenderWidth and gRenderMargin pick the active geometry at
// runtime. Buffer index 0 holds game-space column -gRenderMargin.
//
// 24 gives 288x160, which is 1.8:1. A BG layer narrower than the widened
// frame skips the margin columns, so only 512px-wide maps fill them. The
// overworld map layers are made that wide while widescreen is on, see
// UseWideOverworldBg in src/fieldmap.c.
#define MAX_RENDER_MARGIN 24
#define WIDESCREEN_MARGIN 24
#define MAX_RENDER_WIDTH  (DISPLAY_WIDTH + 2 * MAX_RENDER_MARGIN)

extern int gRenderWidth;
extern int gRenderMargin;

// A screen whose BG maps are 512px wide only so that it can slide between
// pages (the Pokémon summary) has nothing to show in the margins, but they
// would show the edges of its other pages. Setting this to TRUE before a
// frame is drawn blanks the margins of that frame; the renderer clears it
// again, so a screen sets it on every frame from its main callback and it
// stops when the screen does.
extern bool8 gRenderPillarbox;

// Turns the widened frame on or off. Takes effect on the next frame; the
// overworld picks up the wide map layers the next time it sets up its BGs.
void Platform_SetWidescreen(bool32 enabled);

void Platform_StoreSaveFile(void);
void Platform_Log(const char *message);
void Platform_ReportNullTask(u8 taskId, void *creator, const s16 *data);
void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size);
void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame);
u16 Platform_GetKeyInput(void);
void Platform_GetStatus(struct SiiRtcInfo *rtc);
void Platform_SetStatus(struct SiiRtcInfo *rtc);
static void UpdateInternalClock(void);
void Platform_GetDateTime(struct SiiRtcInfo *rtc);
void Platform_SetDateTime(struct SiiRtcInfo *rtc);
void Platform_GetTime(struct SiiRtcInfo *rtc);
void Platform_SetTime(struct SiiRtcInfo *rtc);
void Platform_SetAlarm(u8 *alarmData);

// Sound engine diagnostics, switched on by HNS_AUDIO_LOG. See src/music_player.c.
extern bool8 gAudioLog;
extern u32 gAudioLogFrame;
void AudioLogSongStart(u32 songNum, bool32 gbsEnabled, const void *header, u32 player);
void AudioLogSummary(void);

#endif