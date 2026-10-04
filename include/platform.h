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

// Tall screen. Adds gRenderMarginY lines above and below the 160-line view,
// so widescreen's 288x160 becomes 288x216, exactly 4:3, for 4:3 handhelds.
// Buffer row 0 holds game line -gRenderMarginY. The game still runs 160
// lines: HBlank DMA and the HBlank and VCOUNT interrupts happen once per
// real line, the top margin is drawn with the registers of line 0 and the
// bottom margin with those of line 159.
//
// Only the widened overworld has anything to show there: the margin lines
// draw the 512px map layers and sprites, on frames the overworld marks with
// gRenderMarginYLive. Every other frame gets black bars above and below.
#define TALL_MARGIN 28
#define MAX_RENDER_MARGIN_Y 28
#define MAX_RENDER_HEIGHT (DISPLAY_HEIGHT + 2 * MAX_RENDER_MARGIN_Y)

extern int gRenderHeight;
extern int gRenderMarginY;

// Set to TRUE before a frame is drawn to draw the map in its top and bottom
// margins. The renderer clears it again, so the overworld sets it on every
// frame from its main callback and it stops as soon as another screen takes
// over. See CB2_Overworld in src/overworld.c.
extern bool8 gRenderMarginYLive;

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

// Turns the tall frame on or off, taking effect like Platform_SetWidescreen.
// Meant to be used with widescreen on: the margins are filled from the 512px
// overworld map layers, so without widescreen they stay black.
void Platform_SetTallScreen(bool32 enabled);

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