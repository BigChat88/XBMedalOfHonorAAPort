// Host test for src/xbox_ima4.c: encode/decode round trip and SNR.
#include "../src/xbox_ima4.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

static double roundTrip(const char *name, const short *pcm, int n, double minSnr)
{
    const int      size = Xbox_IMA4_EncodedSize(n);
    unsigned char *enc  = malloc(size);
    const int      blocks = size / XBOX_IMA4_BLOCK_BYTES;
    short         *dec  = malloc(sizeof(short) * blocks * XBOX_IMA4_BLOCK_SAMPLES);

    if (Xbox_IMA4_Encode(pcm, n, enc) != size) {
        printf("FAIL %s: size\n", name);
        failures++;
    }
    for (int b = 0; b < blocks; b++)
        Xbox_IMA4_DecodeBlock(enc + b * XBOX_IMA4_BLOCK_BYTES, XBOX_IMA4_BLOCK_BYTES,
                              dec + b * XBOX_IMA4_BLOCK_SAMPLES);

    double sig = 0, err = 0;
    for (int i = 0; i < n; i++) {
        sig += (double)pcm[i] * pcm[i];
        err += (double)(pcm[i] - dec[i]) * (pcm[i] - dec[i]);
    }
    // padding after the end must settle on the last sample
    for (int i = n + 64; i < blocks * XBOX_IMA4_BLOCK_SAMPLES; i++) {
        if (abs(dec[i] - pcm[n - 1]) > 2000) {
            printf("FAIL %s: padding sample %d = %d\n", name, i, dec[i]);
            failures++;
            break;
        }
    }
    const double snr = 10.0 * log10(sig / (err > 0 ? err : 1e-9));
    printf("%s %s: %d samples -> %d bytes (%.2f:1), SNR %.1f dB\n", snr >= minSnr ? "ok  " : "FAIL", name, n, size,
           (double)n * 2 / size, snr);
    if (snr < minSnr)
        failures++;
    free(enc);
    free(dec);
    return snr;
}

int main(void)
{
    enum { N = 22050 * 2 };
    static short pcm[N];

    for (int i = 0; i < N; i++)
        pcm[i] = (short)(12000 * sin(2 * M_PI * 440 * i / 22050.0));
    roundTrip("sine 440 Hz", pcm, N, 25.0);

    srand(1);
    for (int i = 0; i < N; i++) {
        const double sweep = sin(2 * M_PI * (100 + 4000.0 * i / N) * i / 22050.0);
        pcm[i] = (short)(9000 * sweep + (rand() % 4001 - 2000));
    }
    roundTrip("sweep + noise", pcm, N, 12.0);

    for (int i = 0; i < 1500; i++)
        pcm[i] = (short)((i / 50) % 2 ? 30000 : -30000); // square wave, clipping limits
    roundTrip("square (short)", pcm, 1500, 5.0);

    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures != 0;
}
