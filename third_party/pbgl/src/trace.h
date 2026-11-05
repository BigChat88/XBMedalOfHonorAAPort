#ifndef _PBGL_TRACE_H
#define _PBGL_TRACE_H

/*
 * One-frame GL call trace (OGX MOHAA debugging aid).
 *
 * The application sets pbgl_trace_frames to N; the next N frames log the
 * GL calls that matter for rendering through DbgPrint, and
 * pbgl_swap_buffers() counts the value back down to 0.
 */
#include <xboxkrnl/xboxkrnl.h>

extern int pbgl_trace_frames;

#define PBGL_TRACE(...)                    \
  do {                                     \
    if (pbgl_trace_frames > 0)             \
      DbgPrint("GLT: " __VA_ARGS__);       \
  } while (0)

#endif // _PBGL_TRACE_H
