/*
 * xbox_al.c -- minimal OpenAL 1.1 implementation for the Xbox.
 *
 * Covers exactly what client/snd_openal_new.cpp uses (about 40 functions):
 * buffers, static and queued sources (music/dialogue streaming), 3D listener,
 * gain, pitch and distance attenuation. That way OpenMoHAA's sound system
 * stays untouched and only the "driver" changes.
 *
 * Software mixing at 48 kHz 16-bit stereo on an output thread that feeds the
 * AC97 directly (hal/audio.h): it watches which buffer the analog DMA is
 * playing and refills the finished ones. SDL audio is not used: nxdk's SDL
 * driver only asks for more data once both the analog and the S/PDIF DMA have
 * consumed a buffer, and xemu never advances the S/PDIF one, so it stalled
 * after the first buffer. Every API call takes the lock before touching
 * state.
 *
 * IMA4 (mono, AL_EXT_IMA4 + AL_SOFT_block_alignment) is supported so sounds
 * can stay compressed in RAM: blocks are decoded while mixing, through a small
 * cache shared by all sources. alBufferDataStatic (AL_EXT_STATIC_BUFFER)
 * references the caller's data instead of copying it.
 *
 * Not implemented (reported as unavailable): stereo IMA4, other SOFT
 * extensions, effects/EFX, capture.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <hal/audio.h>
#include <xboxkrnl/xboxkrnl.h>
#include <windows.h>

#define AL_LIBTYPE_STATIC
#include <al.h>
#include <alc.h>
#include <alext.h>

#include "xbox_ima4.h"
#include "xbox_perf.h"

#define XAL_RATE        48000
#define XAL_FRAMES      1024
#define XAL_OUT_BUFFERS 4 // 4 x 1024 frames: ~85 ms queued in the AC97
#define XAL_MASTER_GAIN 2 // the game's mix is quiet on the AC97 (-6 dB)
#define XAL_MAX_BUFFERS 4096
#define XAL_MAX_SOURCES 128
#define XAL_MAX_QUEUE   64

// IMA4 decode cache: direct mapped on (buffer, block)
#define XAL_IMA_CACHE       32
#define XAL_IMA_MAX_SAMPLES 2041 // 1024-byte mono blocks

typedef struct {
    int    used;
    int    channels;    // 1 or 2
    int    bits;        // 4 (IMA4), 8 or 16
    int    freq;
    int    frames;
    void  *data;
    int    size;
    int    isStatic;    // data belongs to the caller (alBufferDataStatic)
    int    unpackAlign; // AL_UNPACK_BLOCK_ALIGNMENT_SOFT, in sample frames
    int    blockFrames; // IMA4: frames per block
    int    blockBytes;  // IMA4: bytes per block
} xalBuffer_t;

typedef struct {
    ALuint buffer;
    int    block;
    short  pcm[XAL_IMA_MAX_SAMPLES];
} xalImaBlock_t;

typedef struct {
    int      used;
    ALenum   state;
    int      looping;
    int      relative;
    float    gain;
    float    pitch;
    float    pos[3];
    float    vel[3];
    float    refDist, maxDist, rolloff;
    // Queue: queue[0..numQueued) with queue[current] playing.
    ALuint   queue[XAL_MAX_QUEUE];
    int      numQueued;
    int      current;
    int      isStatic; // set through AL_BUFFER (not queued)
    uint64_t cursor;   // position in frames, 32.16 fixed point
} xalSource_t;

static SDL_mutex        *xal_lock;
static ALenum            xal_error = AL_NO_ERROR;
static ALenum            xal_distanceModel = AL_INVERSE_DISTANCE_CLAMPED;
static float             xal_listenerPos[3];
static float             xal_listenerOri[6] = {0, 0, -1, 0, 1, 0};
static float             xal_listenerGain = 1.0f;
static xalBuffer_t       xal_buffers[XAL_MAX_BUFFERS];
static xalSource_t       xal_sources[XAL_MAX_SOURCES];
static xalImaBlock_t    *xal_imaCache;

#define LOCK()   do { if (xal_lock) SDL_LockMutex(xal_lock); } while (0)
#define UNLOCK() do { if (xal_lock) SDL_UnlockMutex(xal_lock); } while (0)

static void xal_setError(ALenum e)
{
    if (xal_error == AL_NO_ERROR)
        xal_error = e;
}

static xalBuffer_t *xal_buffer(ALuint id)
{
    if (id == 0 || id > XAL_MAX_BUFFERS || !xal_buffers[id - 1].used)
        return NULL;
    return &xal_buffers[id - 1];
}

static xalSource_t *xal_source(ALuint id)
{
    if (id == 0 || id > XAL_MAX_SOURCES || !xal_sources[id - 1].used) {
        xal_setError(AL_INVALID_NAME);
        return NULL;
    }
    return &xal_sources[id - 1];
}

/*
==============================================================
MIXING (audio thread)
==============================================================
*/

static void xal_spatialize(const xalSource_t *s, const xalBuffer_t *b, float *gl, float *gr)
{
    float gain = s->gain * xal_listenerGain;

    // Stereo buffers: no spatialization.
    if (b->channels == 2) {
        *gl = *gr = gain;
        return;
    }

    float d[3];
    for (int i = 0; i < 3; i++)
        d[i] = s->relative ? s->pos[i] : s->pos[i] - xal_listenerPos[i];
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

    if (xal_distanceModel != AL_NONE && s->rolloff > 0.0f) {
        float ref = s->refDist > 0.0f ? s->refDist : 1.0f;
        float maxd = s->maxDist > ref ? s->maxDist : ref;
        float cd = dist < ref ? ref : (dist > maxd ? maxd : dist);
        if (xal_distanceModel == AL_LINEAR_DISTANCE_CLAMPED || xal_distanceModel == AL_LINEAR_DISTANCE)
            gain *= 1.0f - s->rolloff * (cd - ref) / (maxd - ref > 0.0f ? maxd - ref : 1.0f);
        else
            gain *= ref / (ref + s->rolloff * (cd - ref));
        if (gain < 0.0f)
            gain = 0.0f;
    }

    // Constant-power panning along the listener's "right" vector (at x up).
    float pan = 0.0f;
    if (dist > 0.001f) {
        const float *at = xal_listenerOri, *up = xal_listenerOri + 3;
        float right[3] = {at[1] * up[2] - at[2] * up[1], at[2] * up[0] - at[0] * up[2],
                          at[0] * up[1] - at[1] * up[0]};
        if (s->relative) {
            right[0] = 1.0f; right[1] = 0.0f; right[2] = 0.0f;
        }
        pan = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / dist;
    }
    *gl = gain * sqrtf(0.5f * (1.0f - pan));
    *gr = gain * sqrtf(0.5f * (1.0f + pan));
}

// Decoded samples of IMA4 block `block` of buffer `id`.
static const short *xal_imaBlock(ALuint id, const xalBuffer_t *b, int block)
{
    xalImaBlock_t *e = &xal_imaCache[((unsigned)id * 31u + (unsigned)block) % XAL_IMA_CACHE];
    if (e->buffer != id || e->block != block) {
        Xbox_IMA4_DecodeBlock((const unsigned char *)b->data + block * b->blockBytes, b->blockBytes, e->pcm);
        e->buffer = id;
        e->block = block;
    }
    return e->pcm;
}

static inline int xal_sample(const xalBuffer_t *b, int frame, int ch)
{
    if (b->bits == 16)
        return ((const int16_t *)b->data)[frame * b->channels + ch];
    return ((int)((const uint8_t *)b->data)[frame * b->channels + ch] - 128) << 8;
}

// Bytes <-> sample frames (AL_BYTE_OFFSET); IMA4 counts whole blocks.
static uint32_t xal_bytesToFrames(const xalBuffer_t *b, uint32_t bytes)
{
    if (!b)
        return bytes / 2;
    if (b->bits == 4)
        return bytes / b->blockBytes * b->blockFrames;
    return bytes / (b->channels * b->bits / 8);
}

static uint32_t xal_framesToBytes(const xalBuffer_t *b, uint32_t frames)
{
    if (!b)
        return frames * 2;
    if (b->bits == 4)
        return frames / b->blockFrames * b->blockBytes;
    return frames * (b->channels * b->bits / 8);
}

static void xal_mixSource(xalSource_t *s, int32_t *mix, int frames)
{
    while (frames > 0 && s->state == AL_PLAYING) {
        const xalBuffer_t *b = xal_buffer(s->queue[s->current]);
        if (!b || !b->frames) {
            s->state = AL_STOPPED;
            return;
        }

        float gl, gr;
        xal_spatialize(s, b, &gl, &gr);
        int il = (int)(gl * 256.0f), ir = (int)(gr * 256.0f);
        uint32_t step = (uint32_t)((double)b->freq * s->pitch * 65536.0 / XAL_RATE);
        if (!step)
            step = 1;

        while (frames > 0) {
            uint32_t idx = (uint32_t)(s->cursor >> 16);
            if (idx >= (uint32_t)b->frames)
                break;
            uint32_t frac = (uint32_t)(s->cursor & 0xFFFF);
            uint32_t next = idx + 1 < (uint32_t)b->frames ? idx + 1 : idx;

            int l0, l1;
            if (b->bits == 4) {
                // interpolate within the block only (no second decode at the edge)
                const int    block = idx / b->blockFrames, at = idx % b->blockFrames;
                const short *pcm   = xal_imaBlock(s->queue[s->current], b, block);
                l0 = pcm[at];
                l1 = at + 1 < b->blockFrames && next != idx ? pcm[at + 1] : l0;
            } else {
                l0 = xal_sample(b, idx, 0);
                l1 = xal_sample(b, next, 0);
            }
            int l = l0 + (int)(((int64_t)(l1 - l0) * frac) >> 16);
            int r = l;
            if (b->channels == 2) {
                int r0 = xal_sample(b, idx, 1), r1 = xal_sample(b, next, 1);
                r = r0 + (int)(((int64_t)(r1 - r0) * frac) >> 16);
            }
            mix[0] += (l * il) >> 8;
            mix[1] += (r * ir) >> 8;
            mix += 2;
            frames--;
            s->cursor += step;
        }

        if ((s->cursor >> 16) < (uint64_t)b->frames)
            continue; // the output block ran out, not the buffer

        // End of the current buffer
        s->cursor -= (uint64_t)b->frames << 16;
        if (s->current + 1 < s->numQueued) {
            s->current++;
        } else if (s->looping) {
            s->current = 0;
        } else {
            s->current = s->numQueued - 1;
            s->cursor = 0;
            s->state = AL_STOPPED;
        }
    }
}

// Mixes XAL_FRAMES stereo frames of every playing source into out.
static void xal_render(int16_t *out)
{
    static int32_t mix[XAL_FRAMES * 2];

    memset(mix, 0, sizeof(mix));
    LOCK();
    for (int i = 0; i < XAL_MAX_SOURCES; i++) {
        if (xal_sources[i].used && xal_sources[i].state == AL_PLAYING)
            xal_mixSource(&xal_sources[i], mix, XAL_FRAMES);
    }
    UNLOCK();

    for (int i = 0; i < XAL_FRAMES * 2; i++) {
        int v = mix[i] * XAL_MASTER_GAIN;
        out[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}

/*
==============================================================
OUTPUT (AC97)
==============================================================
*/

extern AC97_DEVICE ac97Device; // hal/audio.c

static unsigned char    *xal_out[XAL_OUT_BUFFERS]; // contiguous memory for the DMA
static unsigned char     xal_outDesc[XAL_OUT_BUFFERS]; // descriptor each buffer went to
static int               xal_outOldest, xal_outQueued;
static volatile int      xal_running;
static HANDLE            xal_thread;

static DWORD WINAPI xal_outputThread(LPVOID arg)
{
    volatile unsigned char *pb = (volatile unsigned char *)ac97Device.mmio;
    (void)arg;

    while (xal_running) {
        const unsigned civ    = pb[0x114] & 31; // descriptor being played
        const int      halted = pb[0x116] & 1;  // DMA stopped: all queued ones done

        // the queued buffers play in order: the ones before the current are done
        while (xal_outQueued > 0 && (halted || xal_outDesc[xal_outOldest] != civ)) {
            xal_outOldest = (xal_outOldest + 1) % XAL_OUT_BUFFERS;
            xal_outQueued--;
        }

        while (xal_outQueued < XAL_OUT_BUFFERS) {
            const int b = (xal_outOldest + xal_outQueued) % XAL_OUT_BUFFERS;
            Xbox_PerfBegin(perfMix);
            xal_render((int16_t *)xal_out[b]);
            Xbox_PerfEnd(XPERF_AUDIO, perfMix);
            xal_outDesc[b] = ac97Device.nextDescriptor;
            XAudioProvideSamples(xal_out[b], XAL_FRAMES * 4, FALSE);
            xal_outQueued++;
        }

        if (halted)
            XAudioPlay(); // ran dry (e.g. a long hitch): restart the DMA
        Sleep(10); // each buffer lasts 21 ms; waking up costs kernel time
    }
    return 0;
}

/*
==============================================================
ALC
==============================================================
*/

static int xal_deviceDummy, xal_contextDummy;

ALC_API ALCdevice *ALC_APIENTRY alcOpenDevice(const ALCchar *devicename)
{
    (void)devicename;

    if (xal_running)
        return (ALCdevice *)&xal_deviceDummy;

    if (!xal_lock && !(xal_lock = SDL_CreateMutex()))
        return NULL;

    for (int i = 0; i < XAL_OUT_BUFFERS; i++) {
        if (!xal_out[i])
            xal_out[i] = MmAllocateContiguousMemoryEx(XAL_FRAMES * 4, 0, 0xFFFFFFFF, 0,
                                                      PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (!xal_out[i])
            return NULL;
    }

    // no callback: the output thread polls the DMA position
    XAudioInit(16, 2, NULL, NULL);
    xal_outOldest = xal_outQueued = 0;
    return (ALCdevice *)&xal_deviceDummy;
}

ALC_API ALCboolean ALC_APIENTRY alcCloseDevice(ALCdevice *device)
{
    (void)device;
    if (xal_running) {
        xal_running = 0;
        WaitForSingleObject(xal_thread, INFINITE);
        CloseHandle(xal_thread);
        xal_thread = NULL;
    }
    XAudioPause();
    XAudioInit(16, 2, NULL, NULL); // reset the DMA, which still points at our buffers
    return ALC_TRUE;
}

ALC_API ALCcontext *ALC_APIENTRY alcCreateContext(ALCdevice *device, const ALCint *attrlist)
{
    (void)device;
    (void)attrlist;
    return (ALCcontext *)&xal_contextDummy;
}

ALC_API ALCboolean ALC_APIENTRY alcMakeContextCurrent(ALCcontext *context)
{
    if (context && !xal_running && xal_out[0]) {
        xal_running = 1;
        xal_thread = CreateThread(NULL, 0, xal_outputThread, NULL, 0, NULL);
        if (!xal_thread) {
            xal_running = 0;
            return ALC_FALSE;
        }
        SetThreadPriority(xal_thread, THREAD_PRIORITY_HIGHEST);
        XAudioPlay();
    }
    return ALC_TRUE;
}

ALC_API void ALC_APIENTRY alcDestroyContext(ALCcontext *context) { (void)context; }
ALC_API void ALC_APIENTRY alcSuspendContext(ALCcontext *context) { (void)context; }
ALC_API void ALC_APIENTRY alcProcessContext(ALCcontext *context) { (void)context; }

ALC_API const ALCchar *ALC_APIENTRY alcGetString(ALCdevice *device, ALCenum param)
{
    (void)device;
    switch (param) {
    case ALC_DEFAULT_DEVICE_SPECIFIER:
    case ALC_DEVICE_SPECIFIER:
    case ALC_DEFAULT_ALL_DEVICES_SPECIFIER:
    case ALC_ALL_DEVICES_SPECIFIER:
        return "Xbox AC97\0"; // double-NUL terminated list
    case ALC_EXTENSIONS:
        return "";
    default:
        return "";
    }
}

ALC_API ALCboolean ALC_APIENTRY alcIsExtensionPresent(ALCdevice *device, const ALCchar *extname)
{
    (void)device;
    (void)extname;
    return ALC_FALSE;
}

ALC_API void ALC_APIENTRY alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values)
{
    (void)device;
    if (size < 1 || !values)
        return;
    values[0] = param == ALC_FREQUENCY ? XAL_RATE : 0;
}

ALC_API ALCenum ALC_APIENTRY alcGetError(ALCdevice *device)
{
    (void)device;
    return ALC_NO_ERROR;
}

/*
==============================================================
GLOBAL STATE
==============================================================
*/

AL_API ALenum AL_APIENTRY alGetError(void)
{
    ALenum e = xal_error;
    xal_error = AL_NO_ERROR;
    return e;
}

AL_API const ALchar *AL_APIENTRY alGetString(ALenum param)
{
    switch (param) {
    case AL_VENDOR:     return "OGX MOHAA";
    case AL_VERSION:    return "1.1 xbox";
    case AL_RENDERER:   return "Software mixer (48 kHz)";
    case AL_EXTENSIONS: return "AL_EXT_IMA4 AL_SOFT_block_alignment AL_EXT_STATIC_BUFFER";
    default:            return "";
    }
}

AL_API ALboolean AL_APIENTRY alIsExtensionPresent(const ALchar *extname)
{
    static const char *const supported[] = {"AL_EXT_IMA4", "AL_SOFT_block_alignment", "AL_EXT_STATIC_BUFFER"};
    for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); i++) {
        if (extname && !SDL_strcasecmp(extname, supported[i]))
            return AL_TRUE;
    }
    return AL_FALSE;
}

AL_API void *AL_APIENTRY alGetProcAddress(const ALchar *fname)
{
    (void)fname;
    return NULL;
}

AL_API ALint AL_APIENTRY alGetInteger(ALenum param)
{
    switch (param) {
    case AL_DISTANCE_MODEL:     return xal_distanceModel;
    default:                    return 0;
    }
}

AL_API void AL_APIENTRY alDistanceModel(ALenum value)
{
    LOCK();
    xal_distanceModel = value;
    UNLOCK();
}

/*
==============================================================
LISTENER
==============================================================
*/

AL_API void AL_APIENTRY alListenerf(ALenum param, ALfloat value)
{
    if (param == AL_GAIN) {
        LOCK();
        xal_listenerGain = value;
        UNLOCK();
    }
}

AL_API void AL_APIENTRY alListenerfv(ALenum param, const ALfloat *values)
{
    LOCK();
    if (param == AL_POSITION)
        memcpy(xal_listenerPos, values, sizeof(xal_listenerPos));
    else if (param == AL_ORIENTATION)
        memcpy(xal_listenerOri, values, sizeof(xal_listenerOri));
    else if (param == AL_GAIN)
        xal_listenerGain = values[0];
    UNLOCK();
}

AL_API void AL_APIENTRY alGetListenerfv(ALenum param, ALfloat *values)
{
    if (param == AL_POSITION)
        memcpy(values, xal_listenerPos, sizeof(xal_listenerPos));
    else if (param == AL_ORIENTATION)
        memcpy(values, xal_listenerOri, sizeof(xal_listenerOri));
    else if (param == AL_GAIN)
        values[0] = xal_listenerGain;
}

/*
==============================================================
BUFFERS
==============================================================
*/

AL_API void AL_APIENTRY alGenBuffers(ALsizei n, ALuint *buffers)
{
    LOCK();
    for (ALsizei i = 0; i < n; i++) {
        buffers[i] = 0;
        for (int j = 0; j < XAL_MAX_BUFFERS; j++) {
            if (!xal_buffers[j].used) {
                memset(&xal_buffers[j], 0, sizeof(xal_buffers[j]));
                xal_buffers[j].used = 1;
                buffers[i] = j + 1;
                break;
            }
        }
        if (!buffers[i])
            xal_setError(AL_OUT_OF_MEMORY);
    }
    UNLOCK();
}

// Sample data held by all buffers (memory budget diagnostics).
static size_t xal_bufferBytes;
static int    xal_bufferCount;

void Com_Printf(const char *fmt, ...);

static void xal_trackBuffer(ALsizei oldSize, ALsizei newSize)
{
    const size_t before = xal_bufferBytes;

    xal_bufferBytes = xal_bufferBytes - oldSize + newSize;
    xal_bufferCount += (newSize > 0) - (oldSize > 0);
    // report every 4 MB step
    if (before >> 22 != xal_bufferBytes >> 22)
        Com_Printf("OpenAL: %d buffers, %u KB of sample data\n", xal_bufferCount,
                   (unsigned)(xal_bufferBytes >> 10));
}

// Sample data copied by alBufferData (malloc'd, outside the engine's zone)
static size_t xal_copiedBytes;

unsigned int Xbox_AL_CopiedBytes(void)
{
    return (unsigned int)xal_copiedBytes;
}

AL_API void AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *buffers)
{
    LOCK();
    for (ALsizei i = 0; i < n; i++) {
        xalBuffer_t *b = xal_buffer(buffers[i]);
        if (b) {
            xal_trackBuffer(b->size, 0);
            if (!b->isStatic) {
                xal_copiedBytes -= b->data ? b->size : 0;
                free(b->data);
            }
            memset(b, 0, sizeof(*b));
            // the id can be reused: forget its decoded blocks
            if (xal_imaCache) {
                for (int j = 0; j < XAL_IMA_CACHE; j++) {
                    if (xal_imaCache[j].buffer == buffers[i])
                        xal_imaCache[j].buffer = 0;
                }
            }
        }
    }
    UNLOCK();
}

AL_API ALboolean AL_APIENTRY alIsBuffer(ALuint buffer)
{
    return buffer == 0 || xal_buffer(buffer) != NULL;
}

// Shared by alBufferData (copy) and alBufferDataStatic (reference).
static void xal_bufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, int isStatic)
{
    int channels, bits;

    switch (format) {
    case AL_FORMAT_MONO8:     channels = 1; bits = 8;  break;
    case AL_FORMAT_MONO16:    channels = 1; bits = 16; break;
    case AL_FORMAT_STEREO8:   channels = 2; bits = 8;  break;
    case AL_FORMAT_STEREO16:  channels = 2; bits = 16; break;
    case AL_FORMAT_MONO_IMA4: channels = 1; bits = 4;  break;
    default:
        xal_setError(AL_INVALID_ENUM);
        return;
    }

    if (bits == 4 && !xal_imaCache) {
        xal_imaCache = malloc(sizeof(xalImaBlock_t) * XAL_IMA_CACHE);
        if (!xal_imaCache) {
            xal_setError(AL_OUT_OF_MEMORY);
            return;
        }
        for (int i = 0; i < XAL_IMA_CACHE; i++)
            xal_imaCache[i].buffer = 0;
    }

    // OpenAL copies the data (the caller may free its own copy), unless static.
    void *copy = (void *)data;
    if (!isStatic) {
        copy = size > 0 ? malloc(size) : NULL;
        if (size > 0 && !copy) {
            xal_setError(AL_OUT_OF_MEMORY);
            return;
        }
        if (copy)
            memcpy(copy, data, size);
    }

    LOCK();
    xalBuffer_t *b = xal_buffer(buffer);
    if (!b) {
        UNLOCK();
        if (!isStatic)
            free(copy);
        xal_setError(AL_INVALID_NAME);
        return;
    }
    int blockFrames = 0, blockBytes = 0;
    if (bits == 4) {
        blockFrames = b->unpackAlign > 0 ? b->unpackAlign : 65; // OpenAL's IMA4 default
        blockBytes = 4 + (blockFrames - 1) / 2;
        if (blockFrames > XAL_IMA_MAX_SAMPLES || (blockFrames - 1) % 2) {
            UNLOCK();
            if (!isStatic)
                free(copy);
            xal_setError(AL_INVALID_VALUE);
            return;
        }
    }
    xal_trackBuffer(b->size, size);
    if (!b->isStatic) {
        xal_copiedBytes -= b->data ? b->size : 0;
        free(b->data);
    }
    if (!isStatic && copy)
        xal_copiedBytes += size;
    // drop cached blocks of the old contents
    if (xal_imaCache) {
        for (int i = 0; i < XAL_IMA_CACHE; i++) {
            if (xal_imaCache[i].buffer == buffer)
                xal_imaCache[i].buffer = 0;
        }
    }
    b->data = copy;
    b->size = size;
    b->isStatic = isStatic;
    b->channels = channels;
    b->bits = bits;
    b->freq = freq;
    b->blockFrames = blockFrames;
    b->blockBytes = blockBytes;
    b->frames = bits == 4 ? size / blockBytes * blockFrames : size / (channels * bits / 8);
    UNLOCK();
}

AL_API void AL_APIENTRY alBufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
{
    xal_bufferData(buffer, format, data, size, freq, 0);
}

// AL_EXT_STATIC_BUFFER: the buffer uses `data` directly; it must stay valid
// until the buffer is deleted or refilled.
void alBufferDataStatic(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
{
    xal_bufferData(buffer, format, data, size, freq, 1);
}

AL_API void AL_APIENTRY alBufferi(ALuint buffer, ALenum param, ALint value)
{
    LOCK();
    xalBuffer_t *b = xal_buffer(buffer);
    if (!b)
        xal_setError(AL_INVALID_NAME);
    else if (param == AL_UNPACK_BLOCK_ALIGNMENT_SOFT)
        b->unpackAlign = value;
    else
        xal_setError(AL_INVALID_ENUM);
    UNLOCK();
}

AL_API void AL_APIENTRY alGetBufferi(ALuint buffer, ALenum param, ALint *value)
{
    xalBuffer_t *b = xal_buffer(buffer);
    if (!b) {
        xal_setError(AL_INVALID_NAME);
        *value = 0;
        return;
    }
    switch (param) {
    case AL_FREQUENCY: *value = b->freq; break;
    case AL_BITS:      *value = b->bits; break;
    case AL_CHANNELS:  *value = b->channels; break;
    case AL_SIZE:      *value = b->size; break;
    default:           xal_setError(AL_INVALID_ENUM); *value = 0; break;
    }
}

/*
==============================================================
SOURCES
==============================================================
*/

static void xal_resetSource(xalSource_t *s)
{
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->state = AL_INITIAL;
    s->gain = 1.0f;
    s->pitch = 1.0f;
    s->refDist = 1.0f;
    s->maxDist = 3.402823466e+38f;
    s->rolloff = 1.0f;
}

AL_API void AL_APIENTRY alGenSources(ALsizei n, ALuint *sources)
{
    LOCK();
    for (ALsizei i = 0; i < n; i++) {
        sources[i] = 0;
        for (int j = 0; j < XAL_MAX_SOURCES; j++) {
            if (!xal_sources[j].used) {
                xal_resetSource(&xal_sources[j]);
                sources[i] = j + 1;
                break;
            }
        }
        if (!sources[i])
            xal_setError(AL_OUT_OF_MEMORY);
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *sources)
{
    LOCK();
    for (ALsizei i = 0; i < n; i++) {
        xalSource_t *s = xal_source(sources[i]);
        if (s)
            memset(s, 0, sizeof(*s));
    }
    UNLOCK();
}

AL_API ALboolean AL_APIENTRY alIsSource(ALuint source)
{
    return source && source <= XAL_MAX_SOURCES && xal_sources[source - 1].used;
}

static uint32_t xal_sampleOffset(const xalSource_t *s)
{
    uint32_t off = (uint32_t)(s->cursor >> 16);
    for (int i = 0; i < s->current; i++) {
        const xalBuffer_t *b = xal_buffer(s->queue[i]);
        if (b)
            off += b->frames;
    }
    return off;
}

static void xal_setSampleOffset(xalSource_t *s, uint32_t off)
{
    s->current = 0;
    while (s->current < s->numQueued) {
        const xalBuffer_t *b = xal_buffer(s->queue[s->current]);
        if (!b || off < (uint32_t)b->frames)
            break;
        off -= b->frames;
        s->current++;
    }
    if (s->current >= s->numQueued)
        s->current = s->numQueued ? s->numQueued - 1 : 0;
    s->cursor = (uint64_t)off << 16;
}

AL_API void AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    if (s) {
        switch (param) {
        case AL_BUFFER:
            if (value == 0) {
                s->numQueued = 0;
                s->isStatic = 0;
            } else {
                s->queue[0] = value;
                s->numQueued = 1;
                s->isStatic = 1;
            }
            s->current = 0;
            s->cursor = 0;
            break;
        case AL_LOOPING:         s->looping = value; break;
        case AL_SOURCE_RELATIVE: s->relative = value; break;
        case AL_SAMPLE_OFFSET:   xal_setSampleOffset(s, (uint32_t)value); break;
        case AL_BYTE_OFFSET: {
            const xalBuffer_t *b = s->numQueued ? xal_buffer(s->queue[0]) : NULL;
            xal_setSampleOffset(s, xal_bytesToFrames(b, (uint32_t)value));
            break;
        }
        case AL_REFERENCE_DISTANCE: s->refDist = (float)value; break;
        case AL_MAX_DISTANCE:       s->maxDist = (float)value; break;
        case AL_ROLLOFF_FACTOR:     s->rolloff = (float)value; break;
        default: break; // AL_DIRECT_CHANNELS_SOFT, resampler...: no effect
        }
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    if (s) {
        switch (param) {
        case AL_GAIN:               s->gain = value; break;
        case AL_PITCH:              s->pitch = value > 0.0f ? value : 1.0f; break;
        case AL_REFERENCE_DISTANCE: s->refDist = value; break;
        case AL_MAX_DISTANCE:       s->maxDist = value; break;
        case AL_ROLLOFF_FACTOR:     s->rolloff = value; break;
        default: break;
        }
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alSource3f(ALuint source, ALenum param, ALfloat v1, ALfloat v2, ALfloat v3)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    if (s) {
        float *dst = param == AL_POSITION ? s->pos : (param == AL_VELOCITY ? s->vel : NULL);
        if (dst) {
            dst[0] = v1;
            dst[1] = v2;
            dst[2] = v3;
        }
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alSourcefv(ALuint source, ALenum param, const ALfloat *values)
{
    alSource3f(source, param, values[0], values[1], values[2]);
}

AL_API void AL_APIENTRY alGetSourcei(ALuint source, ALenum param, ALint *value)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    *value = 0;
    if (s) {
        switch (param) {
        case AL_SOURCE_STATE:      *value = s->state; break;
        case AL_BUFFERS_QUEUED:    *value = s->numQueued; break;
        case AL_BUFFERS_PROCESSED:
            if (s->isStatic)
                *value = 0;
            else if (s->state == AL_STOPPED)
                *value = s->numQueued;
            else
                *value = s->current;
            break;
        case AL_BUFFER:            *value = s->numQueued ? (ALint)s->queue[s->current] : 0; break;
        case AL_LOOPING:           *value = s->looping; break;
        case AL_SOURCE_RELATIVE:   *value = s->relative; break;
        case AL_SAMPLE_OFFSET:     *value = (ALint)xal_sampleOffset(s); break;
        case AL_BYTE_OFFSET: {
            const xalBuffer_t *b = s->numQueued ? xal_buffer(s->queue[0]) : NULL;
            *value = (ALint)xal_framesToBytes(b, xal_sampleOffset(s));
            break;
        }
        default: xal_setError(AL_INVALID_ENUM); break;
        }
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alGetSourceiv(ALuint source, ALenum param, ALint *values)
{
    alGetSourcei(source, param, values);
}

AL_API void AL_APIENTRY alGetSourcef(ALuint source, ALenum param, ALfloat *value)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    *value = 0.0f;
    if (s) {
        switch (param) {
        case AL_GAIN:  *value = s->gain; break;
        case AL_PITCH: *value = s->pitch; break;
        case AL_SEC_OFFSET: {
            const xalBuffer_t *b = s->numQueued ? xal_buffer(s->queue[0]) : NULL;
            *value = b && b->freq ? (float)xal_sampleOffset(s) / b->freq : 0.0f;
            break;
        }
        default: xal_setError(AL_INVALID_ENUM); break;
        }
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alSourcePlay(ALuint source)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    if (s) {
        // Same as OpenAL: playing a stopped/initial source rewinds it;
        // a paused one resumes.
        if (s->state != AL_PAUSED) {
            s->current = 0;
            s->cursor = 0;
        }
        s->state = s->numQueued ? AL_PLAYING : AL_STOPPED;
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alSourceStop(ALuint source)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    if (s) {
        s->state = AL_STOPPED;
        s->current = s->numQueued ? s->numQueued - 1 : 0;
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alSourcePause(ALuint source)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    if (s && s->state == AL_PLAYING)
        s->state = AL_PAUSED;
    UNLOCK();
}

AL_API void AL_APIENTRY alSourceQueueBuffers(ALuint source, ALsizei nb, const ALuint *buffers)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    if (s) {
        if (s->isStatic) {
            s->numQueued = 0;
            s->isStatic = 0;
        }
        for (ALsizei i = 0; i < nb && s->numQueued < XAL_MAX_QUEUE; i++)
            s->queue[s->numQueued++] = buffers[i];
    }
    UNLOCK();
}

AL_API void AL_APIENTRY alSourceUnqueueBuffers(ALuint source, ALsizei nb, ALuint *buffers)
{
    LOCK();
    xalSource_t *s = xal_source(source);
    if (s) {
        int processed = s->state == AL_STOPPED ? s->numQueued : s->current;
        if (nb > processed) {
            xal_setError(AL_INVALID_VALUE);
        } else {
            for (ALsizei i = 0; i < nb; i++)
                buffers[i] = s->queue[i];
            memmove(s->queue, s->queue + nb, (s->numQueued - nb) * sizeof(ALuint));
            s->numQueued -= nb;
            s->current -= s->state == AL_STOPPED ? 0 : nb;
            if (s->current < 0)
                s->current = 0;
        }
    }
    UNLOCK();
}
