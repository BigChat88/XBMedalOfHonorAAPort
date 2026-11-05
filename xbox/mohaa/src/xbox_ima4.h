/*
 * xbox_ima4.h -- IMA ADPCM (Microsoft WAV layout) for the sound memory budget.
 *
 * Sounds are kept in RAM for the whole level. S_LoadSound encodes 16-bit mono
 * PCM to 4-bit IMA ADPCM (4:1) and the OpenAL mixer (xbox_al.c) decodes one
 * block at a time while mixing. The layout is the one of IMA ADPCM WAV files
 * and AL_FORMAT_MONO_IMA4: each block starts with the first sample (int16,
 * little endian), the step index and a reserved byte, then two samples per
 * byte, low nibble first.
 */
#ifndef XBOX_IMA4_H
#define XBOX_IMA4_H

#ifdef __cplusplus
extern "C" {
#endif

#define XBOX_IMA4_BLOCK_BYTES   512
#define XBOX_IMA4_BLOCK_SAMPLES (1 + (XBOX_IMA4_BLOCK_BYTES - 4) * 2) // 1017

// Bytes needed for `samples` mono samples (whole blocks).
int Xbox_IMA4_EncodedSize(int samples);

// Encodes mono 16-bit PCM (samples > 0); the last block is padded with the
// last sample. Returns the number of bytes written (Xbox_IMA4_EncodedSize).
int Xbox_IMA4_Encode(const short *pcm, int samples, unsigned char *out);

// Decodes one block of `blockBytes` bytes into (blockBytes - 4) * 2 + 1 samples.
void Xbox_IMA4_DecodeBlock(const unsigned char *block, int blockBytes, short *out);

#ifdef __cplusplus
}
#endif

#endif // XBOX_IMA4_H
