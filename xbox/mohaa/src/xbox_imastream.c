/*
 * xbox_imastream.c -- streams music and ambience from IMA ADPCM WAVs.
 *
 * libmad took about 6% of the CPU while a level played, and opening an MP3
 * scanned the whole file. tools/convert_music.py writes <name>.ima next to
 * <name>.mp3; S_CodecGetSound (client/snd_codec.c) tries it first when a
 * stream is opened. IMA decoding costs a few instructions per sample, and the
 * file's length comes from its header.
 *
 * The files are WAVs with format tag 0x11: each block starts with one header
 * per channel (first sample as int16, step index, reserved byte), followed by
 * 4-bit samples, low nibble first, with the channels taking turns every 4 bytes.
 */
#include "../../../third_party/openmohaa/code/client/snd_codec.h"

#define IMA_FORMAT_TAG   0x11
#define IMA_MAX_CHANNELS 2
// Blocks read from the disc at once, so a read isn't one trip to the kernel per block
#define IMA_READ_BLOCKS  16

typedef struct {
    int    blockBytes;
    int    blockSamples;  // per channel
    int    framesLeft;    // sample frames still to hand out, from the file's header
    int    blocksLeft;    // blocks still in the file
    int    rawBlocks;     // blocks in raw
    int    rawNext;       // next block of raw to decode
    int    pcmNext;       // next byte of pcm to hand out
    int    pcmBytes;      // bytes decoded in pcm
    byte  *raw;           // IMA_READ_BLOCKS blocks
    short *pcm;           // one decoded block
} imaStream_t;

static const short imaStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static const signed char imaIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static int S_IMA_CodecReadStream(snd_stream_t *stream, int bytes, void *buffer);
static void S_IMA_CodecCloseStream(snd_stream_t *stream);
static void *S_IMA_CodecLoad(const char *filename, snd_info_t *info);

// Not registered: S_CodecGetSound asks for it by name, only for streams
static snd_codec_t ima_codec = {
    "ima", S_IMA_CodecLoad, S_IMA_CodecOpenStream, S_IMA_CodecReadStream, S_IMA_CodecCloseStream, NULL};

static void *S_IMA_CodecLoad(const char *filename, snd_info_t *info)
{
    // whole sounds in memory keep using the MP3 path
    (void)filename;
    (void)info;
    return NULL;
}

static int S_IMA_Read16(const byte *p)
{
    return p[0] | (p[1] << 8);
}

static int S_IMA_Read32(const byte *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

// Reads the RIFF chunks up to the sample data. False if it isn't what convert-music.py writes.
static qboolean S_IMA_ParseHeader(snd_stream_t *stream, int *blockBytes, int *blockSamples, int *frames)
{
    byte tag[12];
    int  dataBytes = 0;
    int  factFrames = 0;
    int  pos;

    *blockBytes = 0;
    if (FS_Read(tag, 12, stream->file) != 12 || memcmp(tag, "RIFF", 4) || memcmp(tag + 8, "WAVE", 4)) {
        return qfalse;
    }
    pos = 12;

    while (pos + 8 <= stream->length) {
        byte head[8];
        int  size;

        if (FS_Read(head, 8, stream->file) != 8) {
            return qfalse;
        }
        size = S_IMA_Read32(head + 4);
        pos += 8;

        if (!memcmp(head, "data", 4)) {
            dataBytes = size;
            break;
        }
        if (!memcmp(head, "fmt ", 4) && size >= 20 && size <= 64) {
            byte fmt[64];
            if (FS_Read(fmt, size, stream->file) != size) {
                return qfalse;
            }
            if (S_IMA_Read16(fmt) != IMA_FORMAT_TAG || S_IMA_Read16(fmt + 14) != 4) {
                return qfalse;
            }
            stream->info.channels = S_IMA_Read16(fmt + 2);
            stream->info.rate     = S_IMA_Read32(fmt + 4);
            *blockBytes           = S_IMA_Read16(fmt + 12);
        } else if (!memcmp(head, "fact", 4) && size == 4) {
            byte fact[4];
            if (FS_Read(fact, 4, stream->file) != 4) {
                return qfalse;
            }
            factFrames = S_IMA_Read32(fact);
        } else {
            FS_Seek(stream->file, pos + size, FS_SEEK_SET);
        }
        pos += size + (size & 1);
        if (size & 1) {
            FS_Seek(stream->file, pos, FS_SEEK_SET);
        }
    }

    if (!*blockBytes || !dataBytes || stream->info.channels < 1 || stream->info.channels > IMA_MAX_CHANNELS
        || *blockBytes <= 4 * stream->info.channels) {
        return qfalse;
    }

    *blockSamples = (*blockBytes - 4 * stream->info.channels) * 2 / stream->info.channels + 1;
    *frames       = dataBytes / *blockBytes * *blockSamples;
    // the last block is padded: the fact chunk has the real length
    if (factFrames > 0 && factFrames < *frames) {
        *frames = factFrames;
    }
    return qtrue;
}

snd_stream_t *S_IMA_CodecOpenStream(const char *filename)
{
    char          name[MAX_QPATH];
    snd_stream_t *stream;
    imaStream_t  *ima;
    int           blockBytes, blockSamples, frames;

    COM_StripExtension(filename, name, sizeof(name));
    Q_strcat(name, sizeof(name), ".ima");

    // no message when there's no conversion: most MP3s get here first
    if (FS_FOpenFileRead(name, NULL, qfalse, qtrue) <= 0) {
        return NULL;
    }

    stream = S_CodecUtilOpen(name, &ima_codec);
    if (!stream) {
        return NULL;
    }

    if (!S_IMA_ParseHeader(stream, &blockBytes, &blockSamples, &frames)) {
        Com_Printf("S_IMA_CodecOpenStream: %s isn't an IMA ADPCM WAV\n", name);
        S_CodecUtilClose(&stream);
        return NULL;
    }

    ima               = Z_Malloc(sizeof(*ima));
    ima->blockBytes   = blockBytes;
    ima->blockSamples = blockSamples;
    ima->framesLeft   = frames;
    ima->blocksLeft   = (frames + blockSamples - 1) / blockSamples;
    ima->rawBlocks    = 0;
    ima->rawNext      = 0;
    ima->pcmNext      = 0;
    ima->pcmBytes     = 0;
    ima->raw          = Z_Malloc(blockBytes * IMA_READ_BLOCKS);
    ima->pcm          = Z_Malloc(blockSamples * stream->info.channels * sizeof(short));

    stream->ptr            = ima;
    stream->info.width     = sizeof(short);
    stream->info.samples   = frames;
    stream->info.size      = frames * stream->info.channels * sizeof(short);
    stream->info.dataofs   = 0;
    stream->info.dataalign = 0;
    return stream;
}

static void S_IMA_DecodeBlock(const byte *block, int blockBytes, int blockSamples, int channels, short *out)
{
    const byte *in = block + 4 * channels;

    (void)blockBytes;

    for (int c = 0; c < channels; c++) {
        int predictor = (short)S_IMA_Read16(block + 4 * c);
        int index     = block[4 * c + 2];

        if (index > 88) {
            index = 88;
        }
        out[c] = (short)predictor;

        for (int i = 0; i < blockSamples - 1; i++) {
            // each channel's samples come in runs of 4 bytes
            const byte b    = in[(i >> 3) * 4 * channels + c * 4 + ((i & 7) >> 1)];
            const int  code = (i & 1) ? (b >> 4) : (b & 15);
            const int  step = imaStepTable[index];
            // the encoder's own rounding (ffmpeg): any other adds noise
            const int  diff = ((2 * (code & 7) + 1) * step) >> 3;

            predictor += (code & 8) ? -diff : diff;
            if (predictor > 32767) {
                predictor = 32767;
            } else if (predictor < -32768) {
                predictor = -32768;
            }
            out[(i + 1) * channels + c] = (short)predictor;

            index += imaIndexTable[code];
            if (index < 0) {
                index = 0;
            } else if (index > 88) {
                index = 88;
            }
        }
    }
}

static int S_IMA_CodecReadStream(snd_stream_t *stream, int bytes, void *buffer)
{
    imaStream_t *ima        = stream->ptr;
    const int    frameBytes = stream->info.channels * sizeof(short);
    byte        *dest       = buffer;
    int          done       = 0;

    bytes -= bytes % frameBytes;

    while (done < bytes) {
        if (ima->pcmNext == ima->pcmBytes) {
            int frames;

            if (!ima->framesLeft) {
                break;
            }
            if (ima->rawNext == ima->rawBlocks) {
                const int want = ima->blocksLeft < IMA_READ_BLOCKS ? ima->blocksLeft : IMA_READ_BLOCKS;
                const int got  = FS_Read(ima->raw, want * ima->blockBytes, stream->file);

                ima->rawBlocks = got / ima->blockBytes;
                ima->rawNext   = 0;
                ima->blocksLeft -= ima->rawBlocks;
                if (!ima->rawBlocks) {
                    ima->framesLeft = 0;
                    break;
                }
            }

            S_IMA_DecodeBlock(ima->raw + ima->rawNext * ima->blockBytes, ima->blockBytes, ima->blockSamples,
                              stream->info.channels, ima->pcm);
            ima->rawNext++;

            frames = ima->framesLeft < ima->blockSamples ? ima->framesLeft : ima->blockSamples;
            ima->framesLeft -= frames;
            ima->pcmNext  = 0;
            ima->pcmBytes = frames * frameBytes;
        }

        {
            const int left = ima->pcmBytes - ima->pcmNext;
            const int n    = bytes - done < left ? bytes - done : left;

            memcpy(dest + done, (byte *)ima->pcm + ima->pcmNext, n);
            ima->pcmNext += n;
            done += n;
        }
    }

    return done;
}

static void S_IMA_CodecCloseStream(snd_stream_t *stream)
{
    imaStream_t *ima;

    if (!stream) {
        return;
    }
    ima = stream->ptr;
    if (ima) {
        Z_Free(ima->raw);
        Z_Free(ima->pcm);
        Z_Free(ima);
    }
    S_CodecUtilClose(&stream);
}
