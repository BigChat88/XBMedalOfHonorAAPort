#ifndef _PBGL_DXT_H
#define _PBGL_DXT_H

/*
 * DXT1 / DXT5 (S3TC) block encoder (OGX MOHAA).
 *
 * glTexImage2D with a GL_COMPRESSED_*_S3TC_* internal format compresses the
 * RGB(A) data here, like a PC driver would. The NV2A samples DXT natively, so
 * textures take 4 (DXT1) or 8 (DXT5) bits per texel instead of 16-32.
 */

#include "GL/gl.h"

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// Bytes per 4x4 block of a DXT internal format we can encode, 0 otherwise.
static inline GLuint dxt_block_bytes(GLenum intfmt) {
  switch (intfmt) {
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: return 8;
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return 16;
    default:                               return 0;
  }
}

// Encodes a w x h image (bytespp 3 = RGB, 4 = RGBA) into DXT blocks, row
// major. blockBytes is 8 (DXT1) or 16 (DXT5). punchAlpha (DXT1 only): texels
// with alpha < 128 become transparent (GL_COMPRESSED_RGBA_S3TC_DXT1_EXT).
void dxt_encode_image(const GLubyte *src, GLuint bytespp, GLuint w, GLuint h, GLubyte *out, GLuint blockBytes,
                      GLboolean punchAlpha);

// Bytes of one level: whole 4x4 blocks, at least one.
static inline GLuint dxt_level_size(GLuint w, GLuint h, GLuint blockBytes) {
  return ((w + 3) / 4) * ((h + 3) / 4) * blockBytes;
}

#endif // _PBGL_DXT_H
