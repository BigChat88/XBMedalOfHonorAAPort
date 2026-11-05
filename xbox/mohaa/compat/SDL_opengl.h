// SDL_opengl.h for the Xbox build: uses pbgl's headers.
//
// nxdk's SDL_opengl.h sees _WIN32 and declares the gl* functions __stdcall,
// but pbgl builds them with the default convention (cdecl). Calling them
// through __stdcall pointers would corrupt the stack on every call, so the
// renderer must see exactly pbgl's declarations.
#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif

// Constants the renderer uses that pbgl does not define (values from the official glext.h).
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_RGB4_S3TC
#define GL_RGB4_S3TC 0x83A1
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
