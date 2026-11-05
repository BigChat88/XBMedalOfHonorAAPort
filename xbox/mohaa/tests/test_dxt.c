// Host test for third_party/pbgl/src/dxt.c: encode, decode with a reference
// S3TC decoder, check PSNR and the block layout.
#include "../../../third_party/pbgl/src/dxt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void unpack565(unsigned c, int *rgb)
{
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    rgb[0] = (r << 3) | (r >> 2);
    rgb[1] = (g << 2) | (g >> 4);
    rgb[2] = (b << 3) | (b >> 2);
}

// reference decoder (S3TC spec)
static void decode(const unsigned char *in, int w, int h, int blockBytes, unsigned char *rgba)
{
    for (int by = 0; by < h; by += 4) {
        for (int bx = 0; bx < w; bx += 4, in += blockBytes) {
            const unsigned char *cb = blockBytes == 16 ? in + 8 : in;
            const unsigned c0 = cb[0] | (cb[1] << 8), c1 = cb[2] | (cb[3] << 8);
            const unsigned idx = cb[4] | (cb[5] << 8) | (cb[6] << 16) | ((unsigned)cb[7] << 24);
            int pal[4][4];
            unpack565(c0, pal[0]);
            unpack565(c1, pal[1]);
            for (int c = 0; c < 3; c++) {
                if (c0 > c1 || blockBytes == 16) {
                    pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
                    pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
                } else {
                    pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
                    pal[3][c] = 0;
                }
            }
            int alev[8];
            unsigned long long abits = 0;
            if (blockBytes == 16) {
                alev[0] = in[0];
                alev[1] = in[1];
                for (int k = 1; k <= 6; k++)
                    alev[k + 1] = in[0] > in[1] ? ((7 - k) * in[0] + k * in[1]) / 7 : 0;
                for (int i = 0; i < 6; i++)
                    abits |= (unsigned long long)in[2 + i] << (8 * i);
            }
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) {
                    if (bx + x >= w || by + y >= h)
                        continue;
                    const int i = y * 4 + x;
                    unsigned char *o = rgba + ((by + y) * w + bx + x) * 4;
                    const int p = (idx >> (2 * i)) & 3;
                    o[0] = pal[p][0];
                    o[1] = pal[p][1];
                    o[2] = pal[p][2];
                    if (blockBytes == 16)
                        o[3] = alev[(abits >> (3 * i)) & 7];
                    else
                        o[3] = (c0 <= c1 && p == 3) ? 0 : 255; // DXT1 transparent index

                }
        }
    }
}

static void check_ex(const char *name, const unsigned char *rgba, int w, int h, int blockBytes, double minPsnr,
                     int punch)
{
    const int      size = dxt_level_size(w, h, blockBytes);
    unsigned char *enc  = malloc(size);
    unsigned char *dec  = malloc(w * h * 4);
    dxt_encode_image(rgba, 4, w, h, enc, blockBytes, punch);
    decode(enc, w, h, blockBytes, dec);

    double err = 0, aerr = 0;
    for (int i = 0; i < w * h; i++) {
        // colour only counts where the texel is visible
        if (punch && rgba[i * 4 + 3] < 128) {
            aerr += (double)dec[i * 4 + 3] * dec[i * 4 + 3];
            continue;
        }
        for (int c = 0; c < 3; c++)
            err += (double)(rgba[i * 4 + c] - dec[i * 4 + c]) * (rgba[i * 4 + c] - dec[i * 4 + c]);
        aerr += (double)(rgba[i * 4 + 3] - dec[i * 4 + 3]) * (rgba[i * 4 + 3] - dec[i * 4 + 3]);
    }
    const double mse  = err / (w * h * 3);
    const double psnr = mse > 0 ? 10 * log10(255.0 * 255.0 / mse) : 99;
    const double amse = aerr / (w * h);
    const double apsnr = amse > 0 ? 10 * log10(255.0 * 255.0 / amse) : 99;
    const int ok = psnr >= minPsnr && ((blockBytes == 8 && !punch) || apsnr >= minPsnr);
    printf("%s %s %dx%d DXT%d: %d bytes, colour PSNR %.1f dB, alpha %.1f dB\n", ok ? "ok  " : "FAIL", name, w, h,
           blockBytes == 8 ? 1 : 5, size, psnr, apsnr);
    if (!ok)
        failures++;
    free(enc);
    free(dec);
}

static void check(const char *name, const unsigned char *rgba, int w, int h, int blockBytes, double minPsnr)
{
    check_ex(name, rgba, w, h, blockBytes, minPsnr, 0);
}

int main(void)
{
    enum { W = 64, H = 64 };
    static unsigned char img[W * H * 4];

    // smooth gradients with an alpha ramp
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned char *p = img + (y * W + x) * 4;
            p[0] = x * 4;
            p[1] = y * 4;
            p[2] = 128 + 60 * sin(x * 0.2);
            p[3] = (x + y) * 2;
        }
    check("gradient", img, W, H, 8, 35);
    check("gradient", img, W, H, 16, 35);

    // noisy texture (worst case for block compression)
    srand(3);
    for (int i = 0; i < W * H * 4; i++)
        img[i] = rand() & 0xFF;
    check("noise", img, W, H, 8, 10);

    // flat colour: only the RGB565 rounding is lost (~37 dB)
    for (int i = 0; i < W * H; i++) {
        img[i * 4 + 0] = 200;
        img[i * 4 + 1] = 100;
        img[i * 4 + 2] = 50;
        img[i * 4 + 3] = 255;
    }
    check("flat", img, W, H, 8, 36);

    // sizes below one block
    check("2x2", img, 2, 2, 16, 36);
    check("1x1", img, 1, 1, 8, 36);

    // binary alpha (foliage, fences): DXT1 with 1-bit alpha must keep the
    // cut-out exactly and the colour of the visible texels
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned char *p = img + (y * W + x) * 4;
            p[0] = x * 4;
            p[1] = 200 - y * 2;
            p[2] = 90;
            p[3] = ((x / 3 + y / 5) % 3) ? 255 : 0;
        }
    check_ex("cut-out", img, W, H, 8, 30, 1);

    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures != 0;
}
