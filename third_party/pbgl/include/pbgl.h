#ifndef _PBGL_PBGL_H
#define _PBGL_PBGL_H

#ifdef __cplusplus
extern "C" {
#endif

#define PBGL_MAXRAM 0x03FFAFFF
#define PBGL_ALLOCFLAGS 0x404

int pbgl_init(int init_pbkit);
void pbgl_set_swap_interval(int interval);
int pbgl_get_swap_interval(void);
void pbgl_swap_buffers(void);
void *pbgl_alloc(unsigned int size);
void pbgl_free(void *addr);
void pbgl_shutdown(void);

// Where the CPU waits for the GPU, and how much it sends (OGX MOHAA). Times are
// in QueryPerformanceCounter ticks; the counters only grow, the reader diffs.
typedef struct {
  unsigned long long vblWait;   // swap: waiting for the vertical blank
  unsigned long long gpuWait;   // swap: waiting for the GPU to finish the frame
  unsigned long long ringStall; // mid-frame: vertex staging ring full
  unsigned long long pushStall; // mid-frame: push buffer full
  unsigned int ringStalls, pushStalls;
  unsigned int draws;
  unsigned int stagedBytes;     // vertex data copied for the GPU
} pbgl_perf_t;

extern pbgl_perf_t pbgl_perf;

#ifdef __cplusplus
}
#endif

#endif // _PBGL_PBGL_H
