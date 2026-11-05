/*
 * dxt.c -- DXT1 / DXT5 block encoder (see dxt.h).
 *
 * Colour: the endpoints are the extreme pixels along the principal axis of the
 * block's colours (power iteration on the covariance), quantised to RGB565;
 * every pixel then takes the nearest of the four palette colours.
 * DXT1 with 1-bit alpha: blocks with transparent pixels use the 3-colour mode.
 * Alpha (DXT5): min/max alpha with the 8 interpolated levels.
 */
#include <string.h>

#include "dxt.h"

static inline GLuint rgb565(int r, int g, int b) {
  return ((GLuint)(r >> 3) << 11) | ((GLuint)(g >> 2) << 5) | (GLuint)(b >> 3);
}

// RGB565 back to 8 bits per channel, as the decoder does
static inline void unpack565(GLuint c, int *rgb) {
  const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  rgb[0] = (r << 3) | (r >> 2);
  rgb[1] = (g << 2) | (g >> 4);
  rgb[2] = (b << 3) | (b >> 2);
}

// punch: DXT1 with 1-bit alpha; pixels with alpha < 128 become transparent
// (3-colour mode for blocks that have any)
static void encode_color(const GLubyte px[16][4], GLubyte *out, GLboolean punch) {
  GLboolean opaque[16];
  int nOpaque = 0;
  for (int i = 0; i < 16; i++) {
    opaque[i] = !punch || px[i][3] >= 128;
    nOpaque += opaque[i];
  }
  const GLboolean threeColor = nOpaque < 16;

  if (nOpaque == 0) {
    // fully transparent block: index 3 everywhere (c0 <= c1)
    memset(out, 0, 4);
    memset(out + 4, 0xFF, 4);
    return;
  }

  if (!threeColor) {
    // a block of one color, very common in lightmaps: no need for the axis
    const GLuint c = rgb565(px[0][0], px[0][1], px[0][2]);
    int i = 1;
    while (i < 16 && rgb565(px[i][0], px[i][1], px[i][2]) == c)
      i++;
    if (i == 16) {
      out[0] = c & 0xFF; out[1] = c >> 8;
      out[2] = c & 0xFF; out[3] = c >> 8;
      memset(out + 4, 0, 4);
      return;
    }
  }

  float mean[3] = {0, 0, 0};
  for (int i = 0; i < 16; i++)
    if (opaque[i])
      for (int c = 0; c < 3; c++)
        mean[c] += px[i][c];
  for (int c = 0; c < 3; c++)
    mean[c] /= (float)nOpaque;

  float cov[6] = {0, 0, 0, 0, 0, 0}; // rr rg rb gg gb bb
  for (int i = 0; i < 16; i++) {
    if (!opaque[i])
      continue;
    const float r = px[i][0] - mean[0], g = px[i][1] - mean[1], b = px[i][2] - mean[2];
    cov[0] += r * r; cov[1] += r * g; cov[2] += r * b;
    cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
  }

  // principal axis: power iteration from the luminance direction
  float axis[3] = {0.299f, 0.587f, 0.114f};
  for (int it = 0; it < 2; it++) {
    const float x = cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2];
    const float y = cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2];
    const float z = cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2];
    float m = x * x + y * y + z * z;
    if (m < 1e-6f)
      break; // flat block
    m = 1.0f / __builtin_sqrtf(m);
    axis[0] = x * m; axis[1] = y * m; axis[2] = z * m;
  }

  int lo = -1, hi = -1;
  float dlo = 1e30f, dhi = -1e30f;
  for (int i = 0; i < 16; i++) {
    if (!opaque[i])
      continue;
    const float d = px[i][0] * axis[0] + px[i][1] * axis[1] + px[i][2] * axis[2];
    if (d < dlo) { dlo = d; lo = i; }
    if (d > dhi) { dhi = d; hi = i; }
  }

  GLuint c0 = rgb565(px[hi][0], px[hi][1], px[hi][2]);
  GLuint c1 = rgb565(px[lo][0], px[lo][1], px[lo][2]);
  // 4-colour mode needs c0 > c1, 3-colour + transparent mode c0 <= c1
  if (threeColor ? c0 > c1 : c0 < c1) {
    const GLuint t = c0; c0 = c1; c1 = t;
  }

  GLuint indices = 0;
  if (c0 != c1 || threeColor) {
    int pal[4][3];
    int nPal;
    unpack565(c0, pal[0]);
    unpack565(c1, pal[1]);
    if (threeColor) {
      // c0, c1, 1/2 c0 + 1/2 c1, transparent
      for (int c = 0; c < 3; c++)
        pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
      nPal = 3;
    } else {
      // c0, c1, 2/3 c0 + 1/3 c1, 1/3 c0 + 2/3 c1
      for (int c = 0; c < 3; c++) {
        pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
        pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
      }
      nPal = 4;
    }
    for (int i = 15; i >= 0; i--) {
      int best = 3;
      if (opaque[i]) {
        int bestd = 0x7fffffff;
        best = 0;
        for (int p = 0; p < nPal; p++) {
          const int dr = px[i][0] - pal[p][0], dg = px[i][1] - pal[p][1], db = px[i][2] - pal[p][2];
          const int d = dr * dr + dg * dg + db * db;
          if (d < bestd) { bestd = d; best = p; }
        }
      }
      indices = (indices << 2) | (GLuint)best;
    }
  }

  out[0] = c0 & 0xFF; out[1] = c0 >> 8;
  out[2] = c1 & 0xFF; out[3] = c1 >> 8;
  out[4] = indices & 0xFF; out[5] = (indices >> 8) & 0xFF;
  out[6] = (indices >> 16) & 0xFF; out[7] = indices >> 24;
}

static void encode_alpha(const GLubyte px[16][4], GLubyte *out) {
  int amin = 255, amax = 0;
  for (int i = 0; i < 16; i++) {
    if (px[i][3] < amin) amin = px[i][3];
    if (px[i][3] > amax) amax = px[i][3];
  }

  out[0] = (GLubyte)amax;
  out[1] = (GLubyte)amin;
  unsigned long long bits = 0;
  if (amax != amin) {
    // a0 > a1: 8 levels, a0, a1, then 6 interpolated from a0 to a1
    int lev[8];
    lev[0] = amax;
    lev[1] = amin;
    for (int k = 1; k <= 6; k++)
      lev[k + 1] = ((7 - k) * amax + k * amin) / 7;
    for (int i = 15; i >= 0; i--) {
      int best = 0, bestd = 256;
      for (int k = 0; k < 8; k++) {
        const int d = px[i][3] > lev[k] ? px[i][3] - lev[k] : lev[k] - px[i][3];
        if (d < bestd) { bestd = d; best = k; }
      }
      bits = (bits << 3) | (unsigned long long)best;
    }
  }
  for (int i = 0; i < 6; i++)
    out[2 + i] = (GLubyte)(bits >> (8 * i));
}

void dxt_encode_image(const GLubyte *src, GLuint bytespp, GLuint w, GLuint h, GLubyte *out, GLuint blockBytes,
                      GLboolean punchAlpha) {
  GLubyte px[16][4];

  for (GLuint by = 0; by < h; by += 4) {
    for (GLuint bx = 0; bx < w; bx += 4) {
      // gather the block; images smaller than 4 texels repeat their edge
      for (GLuint y = 0; y < 4; y++) {
        const GLuint sy = by + y < h ? by + y : h - 1;
        for (GLuint x = 0; x < 4; x++) {
          const GLuint sx = bx + x < w ? bx + x : w - 1;
          const GLubyte *p = src + (sy * w + sx) * bytespp;
          GLubyte *d = px[y * 4 + x];
          d[0] = p[0]; d[1] = p[1]; d[2] = p[2];
          d[3] = bytespp == 4 ? p[3] : 0xFF;
        }
      }
      if (blockBytes == 16) {
        encode_alpha((const GLubyte (*)[4])px, out);
        encode_color((const GLubyte (*)[4])px, out + 8, GL_FALSE);
      } else {
        encode_color((const GLubyte (*)[4])px, out, punchAlpha && bytespp == 4);
      }
      out += blockBytes;
    }
  }
}
