#include <limits.h>

#include "GL/gl.h"
#include "GL/glext.h"
#include "types.h"
#include "error.h"
#include "state.h"
#include "misc.h"

/* GL FUNCTIONS BEGIN */

GL_API void glFogi(GLenum pname, GLint param) {
  switch (pname) {
    case GL_FOG_MODE:
      if (param != pbgl.fog.mode) {
        pbgl.fog.mode = param;
        pbgl.state_dirty = pbgl.fog.dirty = GL_TRUE;
      }
      break;
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
      glFogf(pname, (GLfloat)param);
      break;
    default:
      pbgl_set_error(GL_INVALID_ENUM);
      break;
  }
}

GL_API void glFogf(GLenum pname, GLfloat param) {
  GLfloat *value;

  switch (pname) {
    case GL_FOG_DENSITY: value = &pbgl.fog.density; break;
    case GL_FOG_START:   value = &pbgl.fog.start; break;
    case GL_FOG_END:     value = &pbgl.fog.end; break;
    case GL_FOG_MODE:
      glFogi(pname, (GLint)param);
      return;
    default:
      pbgl_set_error(GL_INVALID_ENUM);
      return;
  }

  if (*value != param) {
    *value = param;
    pbgl.state_dirty = pbgl.fog.dirty = GL_TRUE;
  }
}

GL_API void glFogfv(GLenum pname, const GLfloat *params) {
  if (!params) {
    pbgl_set_error(GL_INVALID_VALUE);
    return;
  }

  switch (pname) {
    case GL_FOG_MODE:
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
      glFogf(pname, params[0]);
      break;

    case GL_FOG_COLOR: {
      // NV097_SET_FOG_COLOR holds red in the low byte
      const GLubyte r = params[0] * 255.f;
      const GLubyte g = params[1] * 255.f;
      const GLubyte b = params[2] * 255.f;
      const GLubyte a = params[3] * 255.f;
      const GLuint color = (a << 24) | (b << 16) | (g << 8) | r;
      if (pbgl.fog.color != color) {
        pbgl.fog.color = color;
        pbgl.state_dirty = pbgl.fog.dirty = GL_TRUE;
      }
      break;
    }

    default:
      pbgl_set_error(GL_INVALID_ENUM);
      break;
  }
}

GL_API void glFogiv(GLenum pname, const GLint *params) {
  if (!params) {
    pbgl_set_error(GL_INVALID_VALUE);
    return;
  }

  switch (pname) {
    case GL_FOG_MODE:
    case GL_FOG_DENSITY:
      glFogi(pname, params[0]);
      break;

    case GL_FOG_COLOR: {
      // "Integer values are interpreted linearly such that
      // the most positive integer maps to 1.0, and the most negative integer maps to -1.0""
      // what the fuck did they mean by this?
      const GLuint r = ((GLdouble)params[0] / (1.0 + (GLdouble)INT_MAX) * 255.0);
      const GLuint g = ((GLdouble)params[1] / (1.0 + (GLdouble)INT_MAX) * 255.0);
      const GLuint b = ((GLdouble)params[2] / (1.0 + (GLdouble)INT_MAX) * 255.0);
      const GLuint a = ((GLdouble)params[3] / (1.0 + (GLdouble)INT_MAX) * 255.0);
      const GLuint color = (a << 24) | (b << 16) | (g << 8) | r;
      if (pbgl.fog.color != color) {
        pbgl.fog.color = color;
        pbgl.state_dirty = pbgl.fog.dirty = GL_TRUE;
      }
      break;
    }

    default:
      pbgl_set_error(GL_INVALID_ENUM);
      break;
  }
}
