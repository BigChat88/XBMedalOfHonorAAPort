/*
 * xbox_glimp.c -- Xbox video layer. Replaces sdl/sdl_glimp.c and sdl/sdl_gamma.c.
 *
 * The GL1 renderer calls OpenGL through the qgl* pointers. Here they point
 * straight at pbgl (OpenGL 1.x on top of pbkit, third_party/pbgl). The few
 * functions pbgl does not implement are defined below as no-ops; none of them
 * is needed for MOHAA's normal rendering.
 */
#define GL_GLEXT_PROTOTYPES
#include <pbgl.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include "../../../third_party/openmohaa/code/renderercommon/tr_common.h"

#include <hal/video.h>
#include <pbkit/pbkit.h>

#define XBOX_VID_WIDTH  640
#define XBOX_VID_HEIGHT 480

int qglMajorVersion, qglMinorVersion;
int qglesMajorVersion, qglesMinorVersion;

void(APIENTRYP qglActiveTextureARB)(GLenum texture);
void(APIENTRYP qglClientActiveTextureARB)(GLenum texture);
void(APIENTRYP qglMultiTexCoord2fARB)(GLenum target, GLfloat s, GLfloat t);
void(APIENTRYP qglLockArraysEXT)(GLint first, GLsizei count);
void(APIENTRYP qglUnlockArraysEXT)(void);

#define GLE(ret, name, ...) name##proc *qgl##name = NULL;
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_ES_1_1_PROCS;
QGL_ES_1_1_FIXED_FUNCTION_PROCS;
QGL_1_3_PROCS;
QGL_1_5_PROCS;
QGL_2_0_PROCS;
QGL_3_0_PROCS;
QGL_ARB_occlusion_query_PROCS;
QGL_ARB_framebuffer_object_PROCS;
QGL_ARB_vertex_array_object_PROCS;
QGL_EXT_direct_state_access_PROCS;
#undef GLE

/*
==============================================================
FUNCTIONS MISSING FROM PBGL
==============================================================
*/

// Display lists: the GL1 renderer only uses them in the r_showtris debug mode.
void APIENTRY glCallList(GLuint list) { (void)list; }

// Direct pixel reads/writes: screenshots and r_speeds.
void APIENTRY glDrawPixels(GLsizei w, GLsizei h, GLenum f, GLenum t, const void *p)
{
    (void)w; (void)h; (void)f; (void)t; (void)p;
}
void APIENTRY glPixelZoom(GLfloat x, GLfloat y) { (void)x; (void)y; }
void APIENTRY glLineStipple(GLint factor, GLushort pattern) { (void)factor; (void)pattern; }

// We always draw to the back buffer.
void APIENTRY glDrawBuffer(GLenum mode) { (void)mode; }

// Only used on debug paths (tr_backend RB_ShowImages, etc.).
void APIENTRY glArrayElement(GLint i) { (void)i; }

// Clip planes: mirrors and portals. TODO: implement in pbgl with the NV2A's
// vertex shader clipping.
void APIENTRY glClipPlane(GLenum plane, const GLdouble *equation) { (void)plane; (void)equation; }

// DXT textures: no pbgl support yet. With textureCompression = TC_NONE the
// renderer decompresses DDS files on the CPU. TODO (64 MB build): upload DXT
// natively, the NV2A supports it.
void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                                     GLsizei height, GLint border, GLsizei imageSize, const void *data)
{
    (void)target; (void)level; (void)internalformat; (void)width; (void)height;
    (void)border; (void)imageSize; (void)data;
}
void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                        GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
                                        const void *data)
{
    (void)target; (void)level; (void)xoffset; (void)yoffset; (void)width; (void)height;
    (void)format; (void)imageSize; (void)data;
}

static void APIENTRY Xbox_LockArrays(GLint first, GLsizei count) { (void)first; (void)count; }
static void APIENTRY Xbox_UnlockArrays(void) {}

/*
==============================================================
INITIALIZATION
==============================================================
*/

static qboolean GLimp_HasExtension(const char *ext)
{
    const char *list = (const char *)glGetString(GL_EXTENSIONS);
    size_t      len  = strlen(ext);

    for (const char *p = list; p && (p = strstr(p, ext)); p += len) {
        if ((p == list || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0'))
            return qtrue;
    }
    return qfalse;
}

static void GLimp_SetProcAddresses(void)
{
#define GLE(ret, name, ...) qgl##name = gl##name;
    QGL_1_1_PROCS;
    QGL_1_1_FIXED_FUNCTION_PROCS;
    QGL_DESKTOP_1_1_PROCS;
    QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
    QGL_1_3_PROCS;
#undef GLE
}

static void GLimp_InitExtensions(void)
{
    GLint units = 0;

    glConfig.textureCompression = TC_NONE;
    glConfig.textureEnvAddAvailable = GLimp_HasExtension("GL_ARB_texture_env_add");

    // pbgl implements the GL 1.3 names (no suffix); the renderer asks for ARB.
    qglActiveTextureARB       = glActiveTexture;
    qglClientActiveTextureARB = glClientActiveTexture;
    qglMultiTexCoord2fARB     = glMultiTexCoord2f;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &units);
    glConfig.numTextureUnits = units;

    // pbgl does not copy vertex arrays, so lock/unlock are no-ops, but the
    // renderer uses them to batch draws of the same surface.
    qglLockArraysEXT   = Xbox_LockArrays;
    qglUnlockArraysEXT = Xbox_UnlockArrays;

    textureFilterAnisotropic = qfalse;
    maxAnisotropy            = 0;
    haveClampToEdge          = qtrue;

    ri.Printf(PRINT_ALL, "pbgl: %d texture units, env_add %s\n", glConfig.numTextureUnits,
              glConfig.textureEnvAddAvailable ? "yes" : "no");
}

void Xbox_ConsoleRelease(void);

void GLimp_Init(qboolean fixedFunction)
{
    (void)fixedFunction;
    ri.Printf(PRINT_ALL, "Initializing pbgl display\n");
    Xbox_ConsoleRelease();

    XVideoSetMode(XBOX_VID_WIDTH, XBOX_VID_HEIGHT, 32, REFRESH_DEFAULT);
    if (pbgl_init(GL_TRUE) < 0)
        ri.Error(ERR_FATAL, "GLimp_Init: pbgl_init failed");
    // Always in sync with the TV's vertical blank: without it the buffers flip
    // mid-scan and the picture tears (very visible in the cinematics).
    // r_swapInterval is ignored, a saved PC-style config has it at 0.
    pbgl_set_swap_interval(1);

    GLimp_SetProcAddresses();
    sscanf((const char *)glGetString(GL_VERSION), "%d.%d", &qglMajorVersion, &qglMinorVersion);

    glConfig.vidWidth         = XBOX_VID_WIDTH;
    glConfig.vidHeight        = XBOX_VID_HEIGHT;
    glConfig.windowAspect     = (float)XBOX_VID_WIDTH / XBOX_VID_HEIGHT;
    glConfig.isFullscreen     = qtrue;
    glConfig.colorBits        = 32;
    glConfig.depthBits        = 24;
    glConfig.stencilBits      = 8;
    glConfig.displayFrequency = 60;
    glConfig.driverType       = GLDRV_ICD;
    glConfig.hardwareType     = GLHW_GENERIC;
    glConfig.deviceSupportsGamma = qfalse;

    Q_strncpyz(glConfig.vendor_string, (const char *)glGetString(GL_VENDOR), sizeof(glConfig.vendor_string));
    Q_strncpyz(glConfig.renderer_string, (const char *)glGetString(GL_RENDERER), sizeof(glConfig.renderer_string));
    Q_strncpyz(glConfig.version_string, (const char *)glGetString(GL_VERSION), sizeof(glConfig.version_string));
    Q_strncpyz(glConfig.extensions_string, (const char *)glGetString(GL_EXTENSIONS),
               sizeof(glConfig.extensions_string));

    GLimp_InitExtensions();

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pbgl_swap_buffers();

    ri.Cvar_Get("r_availableModes", "640x480", CVAR_ROM);

    ri.IN_Init(NULL);
}

void GLimp_Shutdown(void)
{
    ri.IN_Shutdown();
    pbgl_shutdown();
}

void GLimp_EndFrame(void)
{
    // pbgl reports failures (e.g. out of contiguous memory for a texture) only
    // through glGetError, which the renderer never checks.
    static GLenum lastError;
    GLenum        err = glGetError();
    if (err != GL_NO_ERROR && err != lastError) {
        GLint texMem = 0, texCount = 0;
        glGetIntegerv(GL_TEXTURE_MEMORY_USAGE_PBGL, &texMem);
        glGetIntegerv(GL_TEXTURE_COUNT_PBGL, &texCount);
        ri.Printf(PRINT_ALL, "pbgl: GL error 0x%x (%d textures, %d KB texture memory)\n", err, texCount,
                  texMem / 1024);
    }
    lastError = err;

    pbgl_swap_buffers();
}

void GLimp_Minimize(void) {}

void GLimp_LogComment(char *comment)
{
    (void)comment;
}

// No hardware gamma for now (deviceSupportsGamma = qfalse).
void GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256])
{
    (void)red; (void)green; (void)blue;
}
