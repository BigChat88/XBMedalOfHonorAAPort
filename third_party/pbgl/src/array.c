#include <pbkit/pbkit.h>
#include <string.h>

#include "GL/gl.h"
#include "GL/glext.h"
#include "types.h"
#include "error.h"
#include "state.h"
#include "trace.h"
#include "misc.h"
#include "push.h"
#include "array.h"
#include "memory.h"
#include "pbgl.h"

unsigned long long pbgl_perf_ticks(void);

/*
 * Vertex array staging (OGX MOHAA).
 *
 * The NV2A fetches vertex arrays by physical address, but client arrays live
 * in the program's image or heap, whose virtual addresses are not identity
 * mapped (e.g. va 0x013552fc -> pa 0x014092fc). Passing `data & 0x03FFFFFF`
 * made the GPU read unrelated memory and hang. On every draw, the referenced
 * vertex range of each enabled array is copied into a ring of contiguous
 * memory and the GPU is pointed at the copy. This also gives the standard GL
 * semantics: the client may change its arrays right after the draw call.
 * The ring is rewound at swap time, after the GPU went idle.
 */
#define VSTAGE_SIZE (2 * 1024 * 1024)

static GLubyte *vstage;
static GLuint vstage_pos;

static inline GLuint gltype_size(const GLenum gltype) {
  switch(gltype) {
    case GL_FLOAT:         return 4;
    case GL_SHORT:         return 2;
    case GL_UNSIGNED_BYTE: return 1;
    default:               return 4;
  }
}

static const void *vstage_copy(const void *src, GLuint bytes) {
  if (!vstage) {
    vstage = pbgl_mem_alloc(VSTAGE_SIZE);
    vstage_pos = 0;
    if (!vstage)
      return NULL;
  }

  bytes = ALIGN(bytes, MEM_ALIGNMENT);
  if (bytes > VSTAGE_SIZE)
    return NULL;
  if (vstage_pos + bytes > VSTAGE_SIZE) {
    // ran out in the middle of a frame: let the GPU consume what it has
    unsigned long long t0 = pbgl_perf_ticks();
    while (pb_busy());
    pbgl_perf.ringStall += pbgl_perf_ticks() - t0;
    pbgl_perf.ringStalls++;
    vstage_pos = 0;
  }
  pbgl_perf.stagedBytes += bytes;

  void *dst = vstage + vstage_pos;
  memcpy(dst, src, bytes);
  vstage_pos += bytes;
  return dst;
}

void pbgl_array_frame_reset(void) {
  vstage_pos = 0;
}

static inline GLuint varr_to_nvarr(const GLenum arr) {
  static const GLuint arrmap[] = {
    NV2A_VERTEX_ATTR_POSITION, // VARR_POSITION
    NV2A_VERTEX_ATTR_NORMAL,   // VARR_NORMAL
    NV2A_VERTEX_ATTR_DIFFUSE,  // VARR_COLOR1
    NV2A_VERTEX_ATTR_SPECULAR, // VARR_COLOR2
    NV2A_VERTEX_ATTR_FOG,      // VARR_FOG
    NV2A_VERTEX_ATTR_TEXTURE0, // VARR_TEXCOORD
  };
  return arrmap[arr];
}

static inline GLuint gltype_to_nvtype(const GLenum gltype) {
  switch(gltype) {
    case GL_FLOAT:         return NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F;
    case GL_SHORT:         return NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K;
    case GL_UNSIGNED_BYTE: return NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL;
    default:               return 0;
  }
}

static inline void array_set(const GLenum index, const GLenum type, const GLsizei size, const GLsizei stride, const void *data) {
  varray_state_t *arr;

  // don't forget about multitexturing
  if (index == VARR_TEXCOORD)
    arr = &pbgl.tex[pbgl.active_tex_cl].varray;
  else
    arr = &pbgl.varray[index];

  arr->type = type;
  arr->size = size;
  arr->stride = stride;
  arr->data = data;
  arr->dirty = GL_TRUE;
}

// all of these helper functions are basically taken from xgu/xgux

static inline GLuint *array_push_format(GLuint *p, GLuint index, GLuint format, GLuint size, GLuint stride) {
  return push_command_parameter(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + index*4,
    PBGL_MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE, format) |
    PBGL_MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, size) |
    PBGL_MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
}

static inline GLuint *array_push_offset(GLuint *p, GLuint index, const void *data) {
  return push_command_parameter(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + index*4, (GLuint)data);
}

static inline GLuint *array_set_attrib_pointer(GLuint *p, GLuint index, GLuint format, GLuint size, GLuint stride, const void *data) {
  p = array_push_format(p, index, format, size, stride);
  p = array_push_offset(p, index, (const void *)((GLuint)data & 0x03FFFFFF));
  return p;
}

static inline GLuint *array_flush(GLuint *p, const varray_state_t *arr, const GLuint nvtarget)  {
  if (arr->enabled) {
    const GLuint nvtype = gltype_to_nvtype(arr->type);
    p = array_set_attrib_pointer(p, nvtarget, nvtype, arr->size, arr->stride, arr->data);
  } else {
    // if the array is disabled or invalid, make sure we set the current value at least
    p = array_set_attrib_pointer(p, nvtarget, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 0, 0, NULL);
    p = push_command(p, NV097_SET_VERTEX_DATA4F_M + nvtarget * 4 * 4, 4);
    p = push_float(p, arr->value.x);
    p = push_float(p, arr->value.y);
    p = push_float(p, arr->value.z);
    p = push_float(p, arr->value.w);
  }
  return p;
}

// Indices are rebased by `base` (the first staged vertex) as they are pushed.
static inline GLuint *push_elements16(GLuint *p, const GLushort *elements, const GLuint count, const GLuint base) {
  p = push_command(p, 0x40000000 | NV097_ARRAY_ELEMENT16, count / 2);
  for (GLuint i = 0; i < count; i += 2)
    *p++ = (GLuint)(GLushort)(elements[i] - base) | ((GLuint)(GLushort)(elements[i + 1] - base) << 16);
  return p;
}

static inline GLuint *push_elements32(GLuint *p, const GLuint *elements, const GLuint count, const GLuint base) {
  p = push_command(p, 0x40000000 | NV097_ARRAY_ELEMENT32, count);
  for (GLuint i = 0; i < count; i++)
    *p++ = elements[i] - base;
  return p;
}

static inline GLuint *push_draw_arrays(GLuint *p, GLuint start, GLuint count) {
  return push_command_parameter(p, 0x40000000 | NV097_DRAW_ARRAYS,
    PBGL_MASK(NV097_DRAW_ARRAYS_COUNT, (count-1)) |
    PBGL_MASK(NV097_DRAW_ARRAYS_START_INDEX, start));
}

static inline GLuint *draw_elements16(GLuint *p, const GLushort *elements, GLuint count, GLuint base) {
  const GLuint pair_count = count / 2;
  GLuint i = 0;

  while (i < pair_count) {
    // start next batch
    pb_end(p);
    p = pb_begin();
    const GLuint batch_pair_count = umin(pair_count - i, ARRAY_MAX_BATCH);
    p = push_elements16(p, &elements[i * 2], batch_pair_count * 2, base);
    i += batch_pair_count;
  }

  // submit final index if necessary
  if (count & 1) {
    const GLuint index = elements[count - 1];
    p = push_elements32(p, &index, 1, base);
  }

  return p;
}

static inline GLuint *draw_elements32(GLuint *p, const GLuint *elements, GLuint count, GLuint base) {
  while (count > 0) {
    // start next batch
    pb_end(p);
    p = pb_begin();
    const GLuint batch_count = umin(count, ARRAY_MAX_BATCH);
    p = push_elements32(p, elements, batch_count, base);
    elements += batch_count;
    count -= batch_count;
  }
  return p;
}

static inline GLuint *draw_arrays(GLuint *p, GLuint start, GLuint count) {
  while (count > 0) {
    pb_end(p);
    p = pb_begin();
    const GLuint batch_count = imin(count, ARRAY_MAX_BATCH);
    p = push_draw_arrays(p, start, batch_count);
    start += batch_count;
    count -= batch_count;
  }
  return p;
}

/* pbgl internals */

void pbgl_array_reset_all(void) {
  GLuint *p = pb_begin();
  for(GLuint i = 0; i < ARRAY_NV_COUNT; i++)
    p = array_set_attrib_pointer(p, i, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 0, 0, NULL);
  pb_end(p);
}

void pbgl_array_flush_all(void) {
  // set the ones that are enabled, for ones that aren't specify current immediate value

  GLuint *p = pb_begin();

  // set the "regular" arrays
  for (GLuint i = 0; i < VARR_TEXCOORD; ++i) {
    if (pbgl.varray[i].dirty) {
      p = array_flush(p, &pbgl.varray[i], varr_to_nvarr(i));
      pbgl.varray[i].dirty = GL_FALSE;
    }
  }

  // set the texcoord arrays
  for (GLuint i = 0; i < TEXUNIT_COUNT; ++i) {
    if (pbgl.tex[i].enabled && pbgl.tex[i].varray.dirty) {
      p = array_flush(p, &pbgl.tex[i].varray, NV2A_VERTEX_ATTR_TEXTURE0 + i);
      pbgl.tex[i].varray.dirty = GL_FALSE;
    }
  }

  pb_end(p);
}

// Stages vertices [vmin, vmax] of one enabled array and points the GPU at the copy.
static GLuint *array_flush_staged(GLuint *p, varray_state_t *arr, const GLuint nvtarget, GLuint vmin, GLuint vmax) {
  if (!arr->enabled || !arr->data) {
    if (arr->dirty)
      p = array_flush(p, arr, nvtarget);
    arr->dirty = GL_FALSE;
    return p;
  }

  const GLuint elem = arr->size * gltype_size(arr->type);
  const GLuint stride = arr->stride ? arr->stride : elem;
  const GLubyte *src = (const GLubyte *)arr->data + vmin * stride;
  const void *copy = vstage_copy(src, (vmax - vmin) * stride + elem);

  p = array_set_attrib_pointer(p, nvtarget, gltype_to_nvtype(arr->type), arr->size, stride, copy);
  // the pointer has to be set again for the next draw
  arr->dirty = GL_TRUE;
  return p;
}

static void pbgl_array_flush_staged(GLuint vmin, GLuint vmax) {
  GLuint *p = pb_begin();

  for (GLuint i = 0; i < VARR_TEXCOORD; ++i)
    p = array_flush_staged(p, &pbgl.varray[i], varr_to_nvarr(i), vmin, vmax);

  for (GLuint i = 0; i < TEXUNIT_COUNT; ++i) {
    if (pbgl.tex[i].enabled)
      p = array_flush_staged(p, &pbgl.tex[i].varray, NV2A_VERTEX_ATTR_TEXTURE0 + i, vmin, vmax);
  }

  // OGX MOHAA: every draw points the arrays at a fresh copy, but its indices
  // start at 0 again; without this the real NV2A reuses transformed vertices
  // of the previous draw from its vertex cache (triangles of 2D quads drawn
  // at the wrong place or missing; xemu has no such cache)
  p = push_command_parameter(p, NV097_BREAK_VERTEX_BUFFER_CACHE, 0);

  pb_end(p);
}

static void index_range(GLenum idxtype, const void *indices, GLsizei count, GLuint *vmin, GLuint *vmax) {
  GLuint lo = 0xFFFFFFFFu, hi = 0;
  if (idxtype == GL_UNSIGNED_SHORT) {
    const GLushort *e = indices;
    for (GLsizei i = 0; i < count; i++) {
      if (e[i] < lo) lo = e[i];
      if (e[i] > hi) hi = e[i];
    }
  } else {
    const GLuint *e = indices;
    for (GLsizei i = 0; i < count; i++) {
      if (e[i] < lo) lo = e[i];
      if (e[i] > hi) hi = e[i];
    }
  }
  *vmin = lo;
  *vmax = hi;
}

/* GL FUNCTIONS BEGIN */

GL_API void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer) {
  array_set(VARR_POSITION, type, size, stride, pointer);
}

GL_API void glNormalPointer(GLenum type, GLsizei stride, const void *pointer) {
  array_set(VARR_NORMAL, type, 3, stride, pointer);
}

GL_API void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer) {
  array_set(VARR_TEXCOORD, type, size, stride, pointer);
}

GL_API void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer) {
  array_set(VARR_COLOR1, type, size, stride, pointer);
}

GL_API void glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer) {
  array_set(VARR_COLOR2, type, size, stride, pointer);
}

GL_API void glFogCoordPointer(GLenum type, GLsizei stride, const void *pointer) {
  array_set(VARR_FOG, type, 1, stride, pointer);
}

// Logs a draw call with its first vertex (position, unit 0 texcoord, color).
static void trace_draw(const char *what, GLenum prim, GLsizei count, GLuint vmin, GLuint vmax) {
  if (pbgl_trace_frames <= 0)
    return;
  const varray_state_t *pos = &pbgl.varray[VARR_POSITION];
  const varray_state_t *tc = &pbgl.tex[0].varray;
  const varray_state_t *col = &pbgl.varray[VARR_COLOR1];
  PBGL_TRACE("%s prim %u count %d range %u..%u tex0 %d tex1 %d\n", what, prim, count, vmin, vmax,
             pbgl.tex[0].enabled, pbgl.tex[1].enabled);
  PBGL_TRACE("  pos size %d stride %d data %p, st size %d stride %d data %p\n", pos->size, pos->stride, pos->data,
             tc->size, tc->stride, tc->data);
  for (GLuint i = vmin; i <= vmax && i < vmin + 4; ++i) {
    if (pos->enabled && pos->data && pos->type == GL_FLOAT) {
      const GLfloat *v = (const GLfloat *)(pos->data + i * (pos->stride ? pos->stride : pos->size * 4));
      PBGL_TRACE("  v%u pos %d %d %d (x100)\n", i, (int)(v[0] * 100.f), (int)(v[1] * 100.f), (int)(v[2] * 100.f));
    }
    if (tc->enabled && tc->data && tc->type == GL_FLOAT) {
      const GLfloat *t = (const GLfloat *)(tc->data + i * (tc->stride ? tc->stride : tc->size * 4));
      PBGL_TRACE("  v%u st %d %d (x1000)\n", i, (int)(t[0] * 1000.f), (int)(t[1] * 1000.f));
    }
  }
  if (col->enabled && col->data && col->type == GL_UNSIGNED_BYTE) {
    const GLubyte *c = col->data + vmin * (col->stride ? col->stride : col->size);
    PBGL_TRACE("  rgba %u %u %u %u\n", c[0], c[1], c[2], c[3]);
  } else if (!col->enabled) {
    PBGL_TRACE("  no color array\n");
  }
}

GL_API void glDrawArrays(GLenum prim, GLint first, GLsizei count) {
  if (pbgl.imm.active || !count) {
    pbgl_set_error(GL_INVALID_OPERATION);
    return;
  }

  // NV2A prim type values are the same as GL prim types,
  // but with 1 added to accound for OP_END = 0, and they go from 1 to A
  if (prim >= 0xA) {
    pbgl_set_error(GL_INVALID_ENUM);
    return;
  }

  ++prim;

  pbgl_push_reserve();
  pbgl_state_flush();
  pbgl_perf.draws++;
  trace_draw("drawarrays", prim, count, first, first + count - 1);
  pbgl_array_flush_staged(first, first + count - 1);

  GLuint *p = pb_begin();
  p = push_command_parameter(p, NV097_SET_BEGIN_END, prim);

  // the staged copy starts at vertex `first`
  p = draw_arrays(p, 0, count);

  p = push_command_parameter(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  pb_end(p);
}

GL_API void glDrawElements(GLenum prim, GLsizei count, GLenum idxtype, const void *indices) {
  if (pbgl.imm.active || !count) {
    pbgl_set_error(GL_INVALID_OPERATION);
    return;
  }

  if (prim++ >= 0xA) {
    pbgl_set_error(GL_INVALID_ENUM);
    return;
  }

  if (idxtype != GL_UNSIGNED_SHORT && idxtype != GL_UNSIGNED_INT) {
    pbgl_set_error(GL_INVALID_ENUM);
    return;
  }

  GLuint vmin, vmax;
  index_range(idxtype, indices, count, &vmin, &vmax);

  pbgl_push_reserve();
  pbgl_state_flush();
  pbgl_perf.draws++;
  trace_draw("drawelements", prim, count, vmin, vmax);
  pbgl_array_flush_staged(vmin, vmax);

  GLuint *p = pb_begin();
  p = push_command_parameter(p, NV097_SET_BEGIN_END, prim);

  if (idxtype == GL_UNSIGNED_SHORT)
    p = draw_elements16(p, indices, count, vmin);
  else
    p = draw_elements32(p, indices, count, vmin);

  p = push_command_parameter(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  pb_end(p);
}
