/*
 * xbox_perf.c -- frame time breakdown (see xbox_perf.h).
 *
 * Output, every 2 seconds while xbox_perf is 1:
 *   PERF 24.5 fps 40.8 ms | idle 0.0 sv 5.1 (game 4.8) cl 35.2 (scene 20.3
 *        end 14.6) | gpu 9.2 vbl 3.1 ring 0.0/0 push 0.0/0 | 812 draws
 *        1534 KB | audio 6% | free 6012 KB
 * Times are milliseconds per frame. "gpu" and "vbl" are the waits inside the
 * buffer swap (part of "end"); "ring" and "push" are mid-frame waits for the
 * GPU (time/count per frame) when pbgl's vertex ring or push buffer filled up.
 *
 * xbox_perf 2 also draws a summary on screen (View3D::Draw), to read the
 * numbers on a real console, which has no serial port to log to.
 */
#include "../../../third_party/openmohaa/code/qcommon/q_shared.h"
#include "../../../third_party/openmohaa/code/qcommon/qcommon.h"

#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbgl.h>

#include "xbox_perf.h"

int                       xbox_perfOn;
static cvar_t            *xbox_perf;
static unsigned long long perfFreq;
static volatile unsigned long long perfTicks[XPERF_COUNT];
static unsigned long long perfStart;
static int                perfFrames;
static pbgl_perf_t        pbglStart;

// renderer surface types (renderergl1/tr_local.h, surfaceType_t)
#define PERF_SURF_TYPES 17
static const char *const  perfSurfNames[PERF_SURF_TYPES] = {
    "bad", "skip", "face", "grid", "poly", "mark", "flare", "entity", "list",
    "skel", "static", "swipe", "sprite", "terrain", "tris", "md3", "md4"};
static unsigned long long perfSurf[PERF_SURF_TYPES];
static unsigned int       perfSurfCount[PERF_SURF_TYPES];

// render commands (renderergl1/tr_local.h, renderCommand_t)
#define PERF_CMD_TYPES 11
static const char *const  perfCmdNames[PERF_CMD_TYPES] = {
    "end", "color", "2d", "surfs", "sprites", "buffer", "swap", "shot", "video", "mask", "cleardepth"};
static unsigned long long perfCmd[PERF_CMD_TYPES];

#define PERF_OVERLAY_LINES 5
static char               perfOverlay[PERF_OVERLAY_LINES][64];

const char *Xbox_PerfOverlayLine(int line)
{
    if (xbox_perfOn < 2 || line < 0 || line >= PERF_OVERLAY_LINES || !perfOverlay[line][0])
        return 0;
    return perfOverlay[line];
}

void Xbox_PerfCommand(int cmd, unsigned long long ticks)
{
    if (cmd >= 0 && cmd < PERF_CMD_TYPES)
        perfCmd[cmd] += ticks;
}

void Xbox_PerfSurface(int type, unsigned long long ticks)
{
    if (type >= 0 && type < PERF_SURF_TYPES) {
        perfSurf[type] += ticks;
        perfSurfCount[type]++;
    }
}

unsigned long long Xbox_PerfNow(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (unsigned long long)t.QuadPart;
}

void Xbox_PerfAdd(xboxPerfSlot_t slot, unsigned long long ticks)
{
    perfTicks[slot] += ticks;
}

static float Xbox_PerfMs(unsigned long long ticks, int frames)
{
    return (float)((double)ticks * 1000.0 / (double)perfFreq / (frames ? frames : 1));
}

void Xbox_PerfEndFrame(void)
{
    if (!xbox_perf) {
        LARGE_INTEGER f;
        xbox_perf = Cvar_Get("xbox_perf", "0", 0);
        QueryPerformanceFrequency(&f);
        perfFreq = (unsigned long long)f.QuadPart;
    }
    xbox_perfOn = xbox_perf->integer;
    if (!xbox_perfOn) {
        perfStart = 0;
        return;
    }

    const unsigned long long now = Xbox_PerfNow();
    if (!perfStart) {
        // start a measuring period
        perfStart  = now;
        perfFrames = 0;
        pbglStart  = pbgl_perf;
        for (int i = 0; i < XPERF_COUNT; i++)
            perfTicks[i] = 0;
        for (int i = 0; i < PERF_SURF_TYPES; i++)
            perfSurf[i] = perfSurfCount[i] = 0;
        for (int i = 0; i < PERF_CMD_TYPES; i++)
            perfCmd[i] = 0;
        return;
    }

    perfFrames++;
    if (now - perfStart < perfFreq * 2)
        return;

    const int          n       = perfFrames;
    const unsigned long long elapsed = now - perfStart;
    const pbgl_perf_t  pb      = pbgl_perf;
    MM_STATISTICS      ms;

    ms.Length = sizeof(ms);
    MmQueryStatistics(&ms);

    Com_Printf("PERF %.1f fps %.1f ms | idle %.1f sv %.1f (game %.1f) cl %.1f (scene %.1f end %.1f) | "
               "[cgame %.1f rview %.1f backend %.1f] gpu %.1f vbl %.1f ring %.1f/%.1f push %.1f/%.1f | %u draws %u KB | audio %.0f%% | free %lu KB\n",
               n * (double)perfFreq / (double)elapsed, Xbox_PerfMs(elapsed, n),
               Xbox_PerfMs(perfTicks[XPERF_IDLE], n), Xbox_PerfMs(perfTicks[XPERF_SERVER], n),
               Xbox_PerfMs(perfTicks[XPERF_GAME], n), Xbox_PerfMs(perfTicks[XPERF_CLIENT], n),
               Xbox_PerfMs(perfTicks[XPERF_SCENE], n), Xbox_PerfMs(perfTicks[XPERF_END], n),
               Xbox_PerfMs(perfTicks[XPERF_CGAME], n), Xbox_PerfMs(perfTicks[XPERF_RVIEW], n),
               Xbox_PerfMs(perfTicks[XPERF_BACKEND], n),
               Xbox_PerfMs(pb.gpuWait - pbglStart.gpuWait, n), Xbox_PerfMs(pb.vblWait - pbglStart.vblWait, n),
               Xbox_PerfMs(pb.ringStall - pbglStart.ringStall, n), (float)(pb.ringStalls - pbglStart.ringStalls) / n,
               Xbox_PerfMs(pb.pushStall - pbglStart.pushStall, n), (float)(pb.pushStalls - pbglStart.pushStalls) / n,
               (pb.draws - pbglStart.draws) / n, (pb.stagedBytes - pbglStart.stagedBytes) / n / 1024,
               (double)perfTicks[XPERF_AUDIO] * 100.0 / (double)elapsed, ms.AvailablePages * 4);

    Com_sprintf(perfOverlay[0], sizeof(perfOverlay[0]), "%.1f fps  %.1f ms  free %lu KB",
                n * (double)perfFreq / (double)elapsed, Xbox_PerfMs(elapsed, n), ms.AvailablePages * 4);
    Com_sprintf(perfOverlay[1], sizeof(perfOverlay[1]), "sv %.1f  cgame %.1f  rview %.1f",
                Xbox_PerfMs(perfTicks[XPERF_SERVER], n), Xbox_PerfMs(perfTicks[XPERF_CGAME], n),
                Xbox_PerfMs(perfTicks[XPERF_RVIEW], n));
    Com_sprintf(perfOverlay[2], sizeof(perfOverlay[2]), "backend %.1f  flush %.1f  skel %.1f",
                Xbox_PerfMs(perfTicks[XPERF_BACKEND], n), Xbox_PerfMs(perfTicks[XPERF_FLUSH], n),
                Xbox_PerfMs(perfSurf[9], n));
    Com_sprintf(perfOverlay[3], sizeof(perfOverlay[3]), "gpu %.1f  vbl %.1f  audio %.0f%%",
                Xbox_PerfMs(pb.gpuWait - pbglStart.gpuWait, n), Xbox_PerfMs(pb.vblWait - pbglStart.vblWait, n),
                (double)perfTicks[XPERF_AUDIO] * 100.0 / (double)elapsed);
    Com_sprintf(perfOverlay[4], sizeof(perfOverlay[4]), "%u draws  %u KB",
                (pb.draws - pbglStart.draws) / n, (pb.stagedBytes - pbglStart.stagedBytes) / n / 1024);

    {
        // back end: flush, then the surface types that took any time
        char line[512];
        int  len = Com_sprintf(line, sizeof(line), "PERF backend: flush %.1f |", Xbox_PerfMs(perfTicks[XPERF_FLUSH], n));
        for (int i = 0; i < PERF_SURF_TYPES && len < (int)sizeof(line) - 32; i++) {
            if (perfSurfCount[i])
                len += Com_sprintf(line + len, sizeof(line) - len, " %s %.1f (%u)", perfSurfNames[i],
                                   Xbox_PerfMs(perfSurf[i], n), perfSurfCount[i] / n);
        }
        len += Com_sprintf(line + len, sizeof(line) - len, " | cmds:");
        for (int i = 0; i < PERF_CMD_TYPES && len < (int)sizeof(line) - 24; i++) {
            if (perfCmd[i])
                len += Com_sprintf(line + len, sizeof(line) - len, " %s %.1f", perfCmdNames[i], Xbox_PerfMs(perfCmd[i], n));
        }
        Com_Printf("%s\n", line);
    }

    perfStart = 0; // the next frame starts a new period
}
