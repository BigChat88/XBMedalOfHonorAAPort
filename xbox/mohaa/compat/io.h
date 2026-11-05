// Minimal io.h for nxdk.
#pragma once
#include <sys/stat.h> // MSVC pulls it in indirectly through io.h
#ifdef __cplusplus
extern "C" {
#endif
int access(const char *path, int mode);
#define _access access
int unlink(const char *path);
#define _unlink unlink
#ifdef __cplusplus
}
#endif
