/*
 * xbox_ima4.c -- IMA ADPCM encoder/decoder (see xbox_ima4.h).
 */
#include "xbox_ima4.h"

#include <string.h>

static const int ima_steps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static const int ima_index_adjust[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

// Applies one nibble to the predictor/index state (shared by both directions,
// so the encoder tracks exactly what the decoder will reconstruct).
static inline void ima_step(int nibble, int *pred, int *index)
{
    const int step = ima_steps[*index];
    int       diff = step >> 3;

    if (nibble & 4)
        diff += step;
    if (nibble & 2)
        diff += step >> 1;
    if (nibble & 1)
        diff += step >> 2;
    *pred += (nibble & 8) ? -diff : diff;
    if (*pred > 32767)
        *pred = 32767;
    else if (*pred < -32768)
        *pred = -32768;

    *index += ima_index_adjust[nibble];
    if (*index < 0)
        *index = 0;
    else if (*index > 88)
        *index = 88;
}

static inline int ima_encodeSample(int sample, int *pred, int *index)
{
    int step   = ima_steps[*index];
    int diff   = sample - *pred;
    int nibble = 0;

    if (diff < 0) {
        nibble = 8;
        diff   = -diff;
    }
    if (diff >= step) {
        nibble |= 4;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step) {
        nibble |= 2;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step)
        nibble |= 1;

    ima_step(nibble, pred, index);
    return nibble;
}

int Xbox_IMA4_EncodedSize(int samples)
{
    const int blocks = (samples + XBOX_IMA4_BLOCK_SAMPLES - 1) / XBOX_IMA4_BLOCK_SAMPLES;
    return blocks * XBOX_IMA4_BLOCK_BYTES;
}

int Xbox_IMA4_Encode(const short *pcm, int samples, unsigned char *out)
{
    const int      size  = Xbox_IMA4_EncodedSize(samples);
    unsigned char *block = out;
    int            index = 0;

    for (int first = 0; first < samples; first += XBOX_IMA4_BLOCK_SAMPLES, block += XBOX_IMA4_BLOCK_BYTES) {
        // the header holds the block's first sample exactly
        int pred = pcm[first];
        block[0] = (unsigned char)(pred & 0xFF);
        block[1] = (unsigned char)((pred >> 8) & 0xFF);
        block[2] = (unsigned char)index;
        block[3] = 0;

        unsigned char *p = block + 4;
        for (int i = 1; i < XBOX_IMA4_BLOCK_SAMPLES; i += 2) {
            // past the end: hold the last sample (the original stops there too)
            const int s0 = pcm[first + i < samples ? first + i : samples - 1];
            const int s1 = pcm[first + i + 1 < samples ? first + i + 1 : samples - 1];
            const int n0 = ima_encodeSample(s0, &pred, &index);
            const int n1 = ima_encodeSample(s1, &pred, &index);
            *p++         = (unsigned char)(n0 | (n1 << 4));
        }
    }
    return size;
}

void Xbox_IMA4_DecodeBlock(const unsigned char *block, int blockBytes, short *out)
{
    int pred  = (short)(block[0] | (block[1] << 8));
    int index = block[2] > 88 ? 88 : block[2];

    *out++ = (short)pred;
    for (int i = 4; i < blockBytes; i++) {
        ima_step(block[i] & 0x0F, &pred, &index);
        *out++ = (short)pred;
        ima_step(block[i] >> 4, &pred, &index);
        *out++ = (short)pred;
    }
}
