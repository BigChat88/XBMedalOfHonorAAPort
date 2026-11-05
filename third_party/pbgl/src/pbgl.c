#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <pbkit/pbkit.h>

#include "GL/gl.h"
#include "GL/glext.h"
#include "types.h"
#include "matrix.h"
#include "texture.h"
#include "array.h"
#include "error.h"
#include "texenv.h"
#include "push.h"
#include "state.h"
#include "misc.h"
#include "memory.h"
#include "pbgl.h"
#include "trace.h"

int pbgl_trace_frames = 0;

pbgl_perf_t pbgl_perf;

// The commands written since the GPU was last told (see push.h). NULL: none,
// pbkit's write position is the current one.
#define PBGL_KICK_BYTES (32 * 1024)
static uint32_t *pbgl_pending;
static uint32_t *pbgl_last_kick;

uint32_t *pbgl_pb_begin(void) {
  return pbgl_pending ? pbgl_pending : (pb_begin)();
}

void pbgl_kick(void) {
  if (pbgl_pending) {
    uint32_t *p = pbgl_pending;
    pbgl_pending = NULL;
    pbgl_last_kick = p;
    (pb_end)(p);
  }
}

void pbgl_pb_end(uint32_t *p) {
  pbgl_pending = p;
  if (!pbgl_last_kick || p < pbgl_last_kick || (GLuint)(p - pbgl_last_kick) * 4 >= PBGL_KICK_BYTES)
    pbgl_kick();
}

int pbgl_pb_busy(void) {
  pbgl_kick();
  return (pb_busy)();
}

int pbgl_pb_finished(void) {
  pbgl_kick();
  return (pb_finished)();
}

void pbgl_pb_reset(void) {
  pbgl_kick();
  pbgl_last_kick = NULL;
  (pb_reset)();
}

unsigned long long pbgl_perf_ticks(void) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return (unsigned long long)t.QuadPart;
}

// always keep perspective correct texturing and stencil writes enabled
// glStencilMask uses NV097_SET_STENCIL_MASK instead
#define PBGL_CTRL0 (NV097_SET_CONTROL0_TEXTURE_PERSPECTIVE_ENABLE | NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE)

static GLboolean pbkit_initialized = GL_FALSE;
static GLboolean pbkit_do_deinit = GL_FALSE;

// workaround for pbkit enabling the wrong Z buffer mode
static inline void pbgl_target_back_buffer(void) {
  while (pb_finished());
  // override Z-buffer mode set by pbkit's set_draw_buffer() for no apparent reason
  uint32_t *p = pb_begin();
  p = push_command_parameter(p, NV097_SET_CONTROL0, PBGL_CTRL0);
  pb_end(p);
}

int pbgl_init(int init_pbkit) {
  if (pbgl.active)
    return -1; // don't double init

  // init memory allocator, if any
  if (!pbgl_mem_init())
    return -4;

  // remember who has to deinit pbkit later
   pbkit_do_deinit = init_pbkit;

  // user says they've already initialized pbkit
  if (!init_pbkit) pbkit_initialized = GL_TRUE;

  // init pbkit if needed
  if (!pbkit_initialized) {
    int err;
    pb_size(PBGL_PUSHBUFFER_SIZE);
    if ((err = pb_init()) != 0) {
      pbgl_mem_shutdown();
      return -10 - err;
    }
    pbkit_initialized = GL_TRUE;
  }

  pb_show_front_screen();

  GLuint *p = pb_begin();

  // set fixed pipeline mode
  p = push_command_parameter(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
    PBGL_MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_FIXED) |
    PBGL_MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));
  p = push_command_parameter(p, NV097_SET_TRANSFORM_PROGRAM_START, 0);

  // set default clear parameters that never change (for now)
  p = push_command_parameter(p, NV097_SET_CLEAR_RECT_VERTICAL, (pb_back_buffer_height() << 16));
  p = push_command_parameter(p, NV097_SET_CLEAR_RECT_HORIZONTAL, (pb_back_buffer_width() << 16));

  // set some default crap
  p = push_command_boolean(p, NV097_SET_SPECULAR_ENABLE, 1);
  p = push_command_boolean(p, NV097_SET_NORMALIZATION_ENABLE, 0);
  p = push_command_boolean(p, NV097_SET_ZMIN_MAX_CONTROL, 1);
  p = push_command_boolean(p, NV097_SET_COMPRESS_ZBUFFER_EN, 1);
  p = push_command_boolean(p, NV097_SET_WINDOW_CLIP_TYPE, 0);
  p = push_command_parameter(p, NV097_SET_CONTROL0, PBGL_CTRL0);
  p = push_command_parameter(p, NV097_SET_SKIN_MODE, NV097_SET_SKIN_MODE_OFF);
  p = push_command_parameter(p, NV097_SET_SHADER_OTHER_STAGE_INPUT,
        PBGL_MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE1, 0)
      | PBGL_MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE2, 0)
      | PBGL_MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE3, 0));
  p = push_command_parameter(p, NV097_SET_LIGHT_LOCAL_RANGE, 0x7149f2ca);

  // push identity viewport parameters
  p = push_command(p, NV097_SET_VIEWPORT_OFFSET, 4);
  p = push_float(p, 0.f);
  p = push_float(p, 0.f);
  p = push_float(p, 0.f);
  p = push_float(p, 0.f);
  p = push_command(p, NV097_SET_VIEWPORT_SCALE, 4);
  p = push_float(p, 1.f);
  p = push_float(p, 1.f);
  p = push_float(p, 1.f);
  p = push_float(p, 1.f);

  // push default z clip range
  p = push_command_float(p, NV097_SET_CLIP_MIN, 0.f);
  p = push_command_float(p, NV097_SET_CLIP_MAX, (GLfloat)PBGL_Z_MAX);

  pb_end(p);

  // push identity matrices everywhere

  p = pb_begin();
  p = push_command_matrix4x4(p, NV097_SET_PROJECTION_MATRIX, mat4_identity.v);
  p = push_command_matrix4x4(p, NV097_SET_MODEL_VIEW_MATRIX, mat4_identity.v);
  p = push_command_matrix4x4(p, NV097_SET_INVERSE_MODEL_VIEW_MATRIX, mat4_identity.v);
  p = push_command_matrix4x4(p, NV097_SET_COMPOSITE_MATRIX, mat4_identity.v);
  pb_end(p);

  p = pb_begin();
  for (GLuint i = 0, ofs = 0; i < TEXUNIT_COUNT; ++i, ofs += 4 * 4 * 4) {
    p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(i), 0);
    p = push_command_matrix4x4(p, NV097_SET_TEXTURE_MATRIX + ofs, mat4_identity.v);
  }
  pb_end(p);

  // clear all the combiner registers that we won't use
  p = pb_begin();
  for (GLuint i = 4; i < 8; ++i) {
    p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + i * 4, 0);
    p = pb_push1(p, NV097_SET_COMBINER_COLOR_OCW + i * 4, 0);
    p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + i * 4, 0);
    p = pb_push1(p, NV097_SET_COMBINER_ALPHA_OCW + i * 4, 0);
  }
  pb_end(p);

  while (pb_busy());

  // init texture manager
  if (!pbgl_tex_init()) {
    pb_kill();
    pbgl_mem_shutdown();
    pbkit_initialized = GL_FALSE;
    return -3;
  }

  // HACK: preserve some vars that might've been set before init
  const int swap_interval = pbgl.swap_interval;

  // init default state
  pbgl_state_init();

  // disable all vertex arrays
  pbgl_array_reset_all();

  // flush state right away just in case
  pbgl_state_flush();

  // wait for anything that's still happening just in case (probably not necessary)
  while (pb_busy());

  // prepare for drawing the first frame
  pbgl_target_back_buffer();
  // the first time pb_target_back_buffer() is called, it will reenable depth and stencil tests
  // put them back
  p = pb_begin();
  p = push_command_boolean(p, NV097_SET_STENCIL_TEST_ENABLE, pbgl.flags.stencil_test);
  p = push_command_boolean(p, NV097_SET_DEPTH_TEST_ENABLE, pbgl.flags.depth_test);
  pb_end(p);
  // wait until everything is done again
  while (pb_busy());

  pb_reset();
  pbgl_push_mark_start();

  // restore vars
  pbgl.swap_interval = swap_interval;

  // we're live
  pbgl.active = GL_TRUE;

  return 0;
}

void pbgl_set_swap_interval(int interval) {
  pbgl.swap_interval = interval;
}

int pbgl_get_swap_interval(void) {
  return pbgl.swap_interval;
}

/*
 * OGX MOHAA: the wait for the GPU at the end of a frame is put off until the
 * next frame needs the GPU. The game, cgame and the renderer's front end run
 * first and don't touch it, so the CPU gets that work done while the GPU
 * finishes, and most of the time there is nothing left to wait for. The GPU
 * is still idle whenever the push buffer and the vertex ring are rewound, and
 * the frame reaches the screen when it did before.
 *
 * Anything that writes memory the GPU reads has to call this first: draw calls
 * do it in pbgl_push_reserve, texture uploads and deletions in texture.c.
 */
static GLboolean pbgl_sync_pending;

void pbgl_frame_sync(void) {
  if (!pbgl_sync_pending)
    return;
  pbgl_sync_pending = GL_FALSE;

  unsigned long long t0 = pbgl_perf_ticks();
  while (pb_busy());
  pbgl_perf.gpuWait += pbgl_perf_ticks() - t0;

  // the GPU is idle: staged vertex data from the last frame can be overwritten
  pbgl_array_frame_reset();

  pb_reset();
  pbgl_push_mark_start();
}

void pbgl_swap_buffers(void) {
  // a frame that drew nothing still has to rewind
  pbgl_frame_sync();

  // wait up to $swap_interval vblanks
  // same code as in xsm64
  unsigned long long t0 = pbgl_perf_ticks();
  GLint now = pb_get_vbl_counter();
  while ((now - pbgl.last_swap) < pbgl.swap_interval)
      now = pb_wait_for_vbl();
  pbgl.last_swap = now;
  unsigned long long t1 = pbgl_perf_ticks();

  pbgl_perf.vblWait += t1 - t0;

  // swap buffers
  pbgl_target_back_buffer();
  // let the GPU see all of it: nothing else will until the next frame's first draw
  pbgl_kick();
  pbgl_sync_pending = GL_TRUE;

  if (pbgl_trace_frames > 0) {
    PBGL_TRACE("---- end of frame\n");
    --pbgl_trace_frames;
  }
}

/*
 * Push buffer overflow guard (OGX MOHAA).
 *
 * pbkit only rewinds its push buffer in pb_reset(), normally once per frame.
 * A frame that writes past the end makes the GPU fetch garbage and hang (the
 * Quake 3 renderer issues one draw per console character, for instance).
 * Draw calls check how much of the buffer this frame has used; when it gets
 * close to the end they wait for the GPU to go idle and rewind early.
 */
static uint32_t *pbgl_push_start;

// pb_begin() only returns the current write pointer (pbkit's begin/end pairing
// checks exist in DBG builds only), so it can be used to read the position
// without kicking the DMA engine the way pb_end() does.
void pbgl_push_mark_start(void) {
  pbgl_push_start = pb_begin();
}

void pbgl_push_reserve(void) {
  pbgl_frame_sync();

  uint32_t *p = pb_begin();
  if ((GLuint)(p - pbgl_push_start) * 4 < PBGL_PUSHBUFFER_SIZE - PBGL_PUSHBUFFER_SLACK)
    return;

  unsigned long long t0 = pbgl_perf_ticks();
  while (pb_busy());
  pbgl_perf.pushStall += pbgl_perf_ticks() - t0;
  pbgl_perf.pushStalls++;
  pbgl_array_frame_reset();
  pb_reset();
  pbgl_push_mark_start();
}

void *pbgl_alloc(unsigned int size) {
  return pbgl_mem_alloc(size);
}

void pbgl_free(void *addr) {
  pbgl_mem_free(addr);
}

void pbgl_shutdown(void) {
  if (!pbgl.active)
    return;

  if (pbkit_initialized) {
    while (pb_busy());
    // only deinit pbkit if it was us who inited it
    if (pbkit_do_deinit) {
      pb_kill();
      pbkit_initialized = GL_FALSE;
      pbkit_do_deinit = GL_FALSE;
    }
  }

  // free all textures
  pbgl_tex_free();

  // nuke memory allocator
  pbgl_mem_shutdown();

  pbgl.active = GL_FALSE;
}
