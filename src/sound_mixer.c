#ifdef PORTABLE
#include <stdio.h>
#include "global.h"
#include "music_player.h"
#include "sound_mixer.h"
#include "mp2k_common.h"
#include "gba/m4a_internal.h"
#include "platform.h"

#ifdef PORTABLE
    #include "cgb_audio.h"
#endif

#define VCOUNT_VBLANK 160
#define TOTAL_SCANLINES 228

// This file follows the mixer Heart & Soul runs on the GBA, which is ipatix's
// HQ mixer in src/m4a_1.s (SoundMainRAM), not the vanilla one. What that changes:
//  - a sample is compressed when its own header says so, whatever the voice type
//  - reversed and compressed samples go through the same resampling code
//  - a channel whose volume works out to zero is skipped and keeps its position
//  - the mix saturates instead of wrapping around
// The synth voices of that mixer (samples with a size of zero) are not ported;
// Heart & Soul has no data that uses them.

// Voice type bits the mixer looks at. TONEDATA_TYPE_CMP is set by the mixer
// itself on a channel whose sample is compressed.
#define TONEDATA_TYPE_REV 0x10
#define TONEDATA_TYPE_CMP 0x20

// A voice marked "no resample" (TONEDATA_TYPE_FIX) plays one sample per output
// sample on the GBA. Its samples were recorded for the 13379 Hz the vanilla
// games mix at (a few for twice that), but Heart & Soul mixes at 18157 Hz, so
// on the GBA they come out 1.357 times fast. The native build's copies of those
// samples are at 42048 Hz instead of 13379 Hz, so the same speed-up here is
// that ratio applied to 42048 samples a second.
// To play them at the vanilla pitch instead, make GBA_SAMPLE_RATE equal to
// GBA_FIXED_SAMPLE_RATE.
#define GBA_SAMPLE_RATE 18157.16f
#define GBA_FIXED_SAMPLE_RATE 13379.0f
#define NATIVE_FIXED_SAMPLE_RATE 42048.0f

#define BDPCM_BLOCK_SAMPLES 64
#define BDPCM_BLOCK_BYTES 33

static inline void GenerateAudio(struct SoundMixerState *mixer, struct MixerSource *chan, struct WaveData2 *wav, float *outBuffer, u16 samplesPerFrame, float sampleRateReciprocal);
void SampleMixer(struct SoundMixerState *mixer, u32 scanlineLimit, u16 samplesPerFrame, float *outBuffer, u8 dmaCounter, u16 maxBufSize);
static inline bool32 TickEnvelope(struct MixerSource *chan, struct WaveData2 *wav);

void RunMixerFrame(void) {
    struct SoundMixerState *mixer = SOUND_INFO_PTR;

    if (mixer->lockStatus != MIXER_UNLOCKED) {
        return;
    }
    mixer->lockStatus = MIXER_LOCKED;

    u32 maxScanlines = mixer->maxScanlines;
    if (mixer->maxScanlines != 0) {
        u32 vcount = REG_VCOUNT;
        maxScanlines += vcount;
        if (vcount < VCOUNT_VBLANK) {
            maxScanlines += TOTAL_SCANLINES;
        }
    }

    if (mixer->firstPlayerFunc != NULL) {
        mixer->firstPlayerFunc(mixer->firstPlayer);
    }

    mixer->cgbMixerFunc();

    s32 samplesPerFrame = mixer->samplesPerFrame;
    float *outBuffer = mixer->outBuffer;
    s32 dmaCounter = mixer->dmaCounter;

    if (dmaCounter > 1) {
        outBuffer += samplesPerFrame * (mixer->framesPerDmaCycle - (dmaCounter - 1)) * 2;
    }

    //MixerRamFunc mixerRamFunc = ((MixerRamFunc)MixerCodeBuffer);
    SampleMixer(mixer, maxScanlines, samplesPerFrame, outBuffer, dmaCounter, MIXED_AUDIO_BUFFER_SIZE);
    #ifdef PORTABLE
        cgb_audio_generate(samplesPerFrame);
    #endif
}



//__attribute__((target("thumb")))
void SampleMixer(struct SoundMixerState *mixer, u32 scanlineLimit, u16 samplesPerFrame, float *outBuffer, u8 dmaCounter, u16 maxBufSize) {
    u32 reverb = mixer->reverb;
    if (reverb) {
        // The vanilla reverb effect outputs a mono sound from four sources:
        //  - L/R channels as they were mixer->framesPerDmaCycle frames ago
        //  - L/R channels as they were (mixer->framesPerDmaCycle - 1) frames ago
        float *tmp1 = outBuffer;
        float *tmp2;
        if (dmaCounter == 2) {
            tmp2 = mixer->outBuffer;
        } else {
            tmp2 = outBuffer + samplesPerFrame * 2;
        }
        uf16 i = 0;
        do {
            float s = tmp1[0] + tmp1[1] + tmp2[0] + tmp2[1];
            s *= ((float)reverb / 512.0f);
            tmp1[0] = tmp1[1] = s;
            tmp1+=2;
            tmp2+=2;
        }
        while(++i < samplesPerFrame);
    } else {
        // memset(outBuffer, 0, samplesPerFrame);
        // memset(outBuffer + maxBufSize, 0, samplesPerFrame);
        for (int i = 0; i < samplesPerFrame; i++) {
            float *dst = &outBuffer[i*2];
            dst[1] = dst[0] = 0.0f;
        }
    }

    float sampleRateReciprocal = mixer->sampleRateReciprocal;
    sf8 numChans = mixer->numChans;
    struct MixerSource *chan = mixer->chans;

    for (int i = 0; i < numChans; i++, chan++) {
        struct WaveData2 *wav = chan->wav;

        if (scanlineLimit != 0) {
            uf16 vcount = REG_VCOUNT;
            if (vcount < VCOUNT_VBLANK) {
                vcount += TOTAL_SCANLINES;
            }
            if (vcount >= scanlineLimit) {
                goto returnEarly;
            }
        }

        if (TickEnvelope(chan, wav))
        {

            GenerateAudio(mixer, chan, wav, outBuffer, samplesPerFrame, sampleRateReciprocal);
        }
    }

    // The GBA mixer saturates when it converts its mix to 8 bits. Do the same,
    // which also keeps the reverb above from feeding on out-of-range values.
    for (int i = 0; i < samplesPerFrame * 2; i++) {
        if (outBuffer[i] > 1.0f) {
            outBuffer[i] = 1.0f;
        } else if (outBuffer[i] < -1.0f) {
            outBuffer[i] = -1.0f;
        }
    }
returnEarly:
    mixer->lockStatus = MIXER_UNLOCKED;
}

// Returns TRUE if channel is still active after moving envelope forward a frame
//__attribute__((target("thumb")))
static inline bool32 TickEnvelope(struct MixerSource *chan, struct WaveData2 *wav) {
    // MP2K envelope shape
    //                                                                 |
    // (linear)^                                                       |
    // Attack / \Decay (exponential)                                   |
    //       /   \_                                                    |
    //      /      '.,        Sustain                                  |
    //     /          '.______________                                 |
    //    /                           '-.       Echo (linear)          |
    //   /                 Release (exp) ''--..|\                      |
    //  /                                        \                     |

    u8 status = chan->status;
    if ((status & 0xC7) == 0) {
        return FALSE;
    }

    u32 env;
    if (status & 0x80) {
        if (status & 0x40) {
            // Init and stop cancel each other out
            chan->status = 0;
            return FALSE;
        }

        // Init channel. chan->ct holds the number of samples to skip, which
        // is how a cry is started partway through.
        if (wav == NULL) {
            chan->status = 0;
            return FALSE;
        }
        s32 samplesLeft = wav->size - chan->ct;
        if (wav->size == 0 || samplesLeft <= 0) {
            if (gAudioLog && wav->size == 0) {
                fprintf(stderr, "audio: synth voice (sample size 0) is not supported\n");
            }
            chan->status = 0;
            return FALSE;
        }
        chan->type &= ~TONEDATA_TYPE_CMP;
        if (wav->compressionFlags1 & 1) {
            chan->type |= TONEDATA_TYPE_CMP;
        }
        chan->ct = samplesLeft;
        chan->current = wav->data + (wav->size - samplesLeft);
        chan->fw = 0;
        chan->blockCount = 0;
        status = 3;
        if (wav->loopFlags & 0xC0) {
            status |= 0x10;
        }
        env = chan->attack;
        if (env >= 0xFF) {
            env = 0xFF;
            status--;
        }
    } else if (status & 4) {
        // Note-wise echo
        u8 echoLen = chan->echoLen;
        chan->echoLen = echoLen - 1;
        if (echoLen <= 1) {
            chan->status = 0;
            return FALSE;
        }
        return TRUE;
    } else if (status & 0x40) {
        // Release
        env = chan->envelopeVol * chan->release / 256U;
        if (env == 0 || env <= chan->echoVol) {
        released:
            env = chan->echoVol;
            if (env == 0) {
                chan->status = 0;
                return FALSE;
            }
            status |= 4;
        }
    } else {
        env = chan->envelopeVol;
        switch (status & 3) {
        case 2:
            // Decay
            env = env * chan->decay / 256U;
            if (env <= chan->sustain) {
                env = chan->sustain;
                if (env == 0) {
                    goto released;
                }
                status--;
            }
            break;
        case 3:
            // Attack
            env += chan->attack;
            if (env >= 0xFF) {
                env = 0xFF;
                status--;
            }
            break;
        case 1: // Sustain
        default:
            break;
        }
    }

    chan->status = status;
    chan->envelopeVol = env;
    return TRUE;
}

// Compressed samples are 64-sample blocks of 33 bytes: the first sample, then
// 63 four-bit deltas. A few decoded blocks are kept, since interpolation reads
// across block edges and several channels can play compressed samples at once.
struct DecodedBlock {
    const struct WaveData2 *wav;
    u32 block;
    s8 samples[BDPCM_BLOCK_SAMPLES];
};

static struct DecodedBlock sDecodedBlocks[8];
static u8 sNextDecodedBlock;
extern const s8 gDeltaEncodingTable[];

static const s8 *DecodeBlock(const struct WaveData2 *wav, u32 block) {
    struct DecodedBlock *decoded;
    const u8 *src;
    s8 sample;
    int i;

    for (i = 0; i < (int)ARRAY_COUNT(sDecodedBlocks); i++) {
        decoded = &sDecodedBlocks[i];
        if (decoded->wav == wav && decoded->block == block) {
            return decoded->samples;
        }
    }

    decoded = &sDecodedBlocks[sNextDecodedBlock];
    sNextDecodedBlock = (sNextDecodedBlock + 1) % ARRAY_COUNT(sDecodedBlocks);
    decoded->wav = wav;
    decoded->block = block;

    src = (const u8 *)wav->data + block * BDPCM_BLOCK_BYTES;
    sample = (s8)*src++;
    decoded->samples[0] = sample;
    sample += gDeltaEncodingTable[*src++ & 0xF];
    decoded->samples[1] = sample;
    for (i = 2; i < BDPCM_BLOCK_SAMPLES; i += 2) {
        u8 deltas = *src++;

        sample += gDeltaEncodingTable[deltas >> 4];
        decoded->samples[i] = sample;
        sample += gDeltaEncodingTable[deltas & 0xF];
        decoded->samples[i + 1] = sample;
    }
    return decoded->samples;
}

// A channel's place in its sample is the number of samples it has left.
// Reversed channels walk the sample from its end, so this turns that count
// into the sample to read. Outside the sample it gives silence.
static inline float SampleAt(const struct WaveData2 *wav, u8 type, s32 samplesLeft) {
    s32 index;

    if (type & TONEDATA_TYPE_REV) {
        index = samplesLeft - 1;
    } else {
        index = wav->size - samplesLeft;
    }
    if (index < 0 || index >= (s32)wav->size) {
        return 0.0f;
    }
    if (type & TONEDATA_TYPE_CMP) {
        return DecodeBlock(wav, index / BDPCM_BLOCK_SAMPLES)[index % BDPCM_BLOCK_SAMPLES];
    }
    return wav->data[index];
}

//__attribute__((target("thumb")))
static inline void GenerateAudio(struct SoundMixerState *mixer, struct MixerSource *chan, struct WaveData2 *wav, float *outBuffer, u16 samplesPerFrame, float sampleRateReciprocal) {/*, [[[]]]) {*/
    u32 v = chan->envelopeVol * (mixer->masterVol + 1);
    u32 volR = (chan->rightVol * v) >> 13;
    u32 volL = (chan->leftVol * v) >> 13;
    chan->envelopeVolR = volR;
    chan->envelopeVolL = volL;
    if (volR == 0 && volL == 0) {
        return;
    }

    // The GBA mixer halves these, rounding up, and mixes at twice the sample
    // value, into a range where 16384 is full scale.
    float envR = (float)(((volR + 1) >> 1) * 2) / 16384.0f;
    float envL = (float)(((volL + 1) >> 1) * 2) / 16384.0f;

    u8 type = chan->type;
    // Only plain forward samples loop.
    s32 loopLen = 0;
    if ((chan->status & 0x10) && (type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV)) == 0) {
        loopLen = wav->size - wav->loopStart;
        if (loopLen < 0) {
            loopLen = 0;
        }
    }

    float romSamplesPerOutputSample;
    if (type & TONEDATA_TYPE_FIX) {
        romSamplesPerOutputSample = (GBA_SAMPLE_RATE / GBA_FIXED_SAMPLE_RATE) * NATIVE_FIXED_SAMPLE_RATE * sampleRateReciprocal;
    } else {
        romSamplesPerOutputSample = chan->freq * sampleRateReciprocal;
    }

    s32 samplesLeftInWav = chan->ct;
    float finePos = chan->fw;
    // Use linear interpolation to calculate a value between the current sample in the wav
    // and the next sample.
    float b = SampleAt(wav, type, samplesLeftInWav);
    float m;
    if (samplesLeftInWav > 1) {
        m = SampleAt(wav, type, samplesLeftInWav - 1) - b;
    } else if (loopLen != 0) {
        m = SampleAt(wav, type, loopLen) - b;
    } else {
        m = 0.0f;
    }

    for (u16 i = 0; i < samplesPerFrame; i++, outBuffer+=2) {
        float sample = (finePos * m) + b;

        outBuffer[1] += sample * envR;
        outBuffer[0] += sample * envL;

        finePos += romSamplesPerOutputSample;
        if (finePos >= 1.0f) {
            s32 newCoarsePos = finePos;

            finePos -= newCoarsePos;
            samplesLeftInWav -= newCoarsePos;
            if (samplesLeftInWav <= 0) {
                if (loopLen == 0) {
                    chan->status = 0;
                    return;
                }
                do {
                    samplesLeftInWav += loopLen;
                } while (samplesLeftInWav <= 0);
            }
            b = SampleAt(wav, type, samplesLeftInWav);
            if (samplesLeftInWav > 1) {
                m = SampleAt(wav, type, samplesLeftInWav - 1) - b;
            } else if (loopLen != 0) {
                m = SampleAt(wav, type, loopLen) - b;
            } else {
                m = 0.0f;
            }
        }
    }

    chan->fw = finePos;
    chan->ct = samplesLeftInWav;
    chan->current = wav->data + (wav->size - samplesLeftInWav);
}
#endif //PORTABLE
