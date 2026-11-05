// Minimal sys/stat.h for nxdk: just enough for stat()/_stat() on FATX.
#pragma once
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stat {
    unsigned int st_mode;
    long st_size;
    time_t st_mtime;
    time_t st_ctime;
    time_t st_atime;
};
#define _stat stat

#define S_IFMT  0xF000
#define S_IFDIR 0x4000
#define S_IFREG 0x8000
#define _S_IFDIR S_IFDIR
#define _S_IFREG S_IFREG
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

int stat(const char *path, struct stat *st);
int mkdir(const char *path, int mode);

#ifdef __cplusplus
}
#endif
