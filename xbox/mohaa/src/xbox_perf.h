/*
 * xbox_perf.h -- frame time breakdown (cvar xbox_perf 1).
 *
 * The engine brackets the parts of a frame with Xbox_PerfBegin/End; every two
 * seconds xbox_perf.c prints the average per frame of each part, pbgl's waits
 * for the GPU and the audio mixer's load to the console (and the serial log).
 */
#ifndef XBOX_PERF_H
#define XBOX_PERF_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XPERF_IDLE,   // Com_Frame: waiting for com_maxfps
    XPERF_SERVER, // SV_Frame (includes XPERF_GAME)
    XPERF_GAME,   // the game module's RunFrame
    XPERF_CLIENT, // CL_Frame (includes XPERF_SCENE and XPERF_END)
    XPERF_SCENE,  // cgame, UI and the renderer's front end (drawing commands)
    XPERF_END,    // RE_EndFrame: renderer back end and the buffer swap
    XPERF_CGAME,  // cgame's DrawActiveFrame (includes the 3D view's front end)
    XPERF_RVIEW,  // RE_RenderScene: renderer front end of a 3D view
    XPERF_BACKEND,// RB_ExecuteRenderCommands: renderer back end (GL calls)
    XPERF_FLUSH,  // RB_EndSurface: shader stages and GL calls of a batch
    XPERF_AUDIO,  // audio mixer thread (xbox_al.c)
    XPERF_COUNT
} xboxPerfSlot_t;

unsigned long long Xbox_PerfNow(void);
// xbox_perf 2: the lines to draw on screen; NULL past the last one or while off
const char        *Xbox_PerfOverlayLine(int line);
void               Xbox_PerfAdd(xboxPerfSlot_t slot, unsigned long long ticks);
void               Xbox_PerfEndFrame(void);
// time spent filling tess with a surface of type `type` (renderer back end)
void               Xbox_PerfSurface(int type, unsigned long long ticks);
// time spent on a render command of type `cmd` (renderer back end)
void               Xbox_PerfCommand(int cmd, unsigned long long ticks);

// Reading the timer is an I/O port access: the hooks do nothing unless xbox_perf
// is on, some of them run thousands of times per frame
extern int xbox_perfOn;

#define Xbox_PerfBegin(var) unsigned long long var = xbox_perfOn ? Xbox_PerfNow() : 0
#define Xbox_PerfEnd(slot, var) \
    do { \
        if (xbox_perfOn) \
            Xbox_PerfAdd(slot, Xbox_PerfNow() - (var)); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif // XBOX_PERF_H
