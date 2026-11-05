// Force-included (-include) into every module of the Xbox build.
// Bridges the gap between nxdk's libc (pdclib + partial winapi) and what
// OpenMoHAA expects from Windows/MSVC.
#pragma once

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

int _stricmp(const char *s1, const char *s2);
int _strnicmp(const char *s1, const char *s2, size_t n);

static inline int strcasecmp(const char *a, const char *b) { return _stricmp(a, b); }
static inline int strncasecmp(const char *a, const char *b, size_t n) { return _strnicmp(a, b, n); }

// POSIX functions pdclib lacks; implemented in xbox/mohaa/src/xbox_posix.c
int putenv(char *string);
int rmdir(const char *path);

// fopen that converts '/' to '\' (the Xbox kernel only accepts backslashes)
FILE *xbox_fopen(const char *path, const char *mode);
// fseek that also clears the error flag nxdk leaves behind at end of file
int xbox_fseek(FILE *stream, long offset, int whence);
// fread that reads big blocks directly (see xbox_posix.c)
size_t xbox_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
// fwrite that writes big blocks directly (see xbox_posix.c)
size_t xbox_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

// pdclib ignores %f/%e/%g in printf and scanf, and xboxrt's strtod family is
// a stub. Redirect them to working implementations:
//   sprintf family    -> third_party/printf (eyalroz/printf)
//   everything else   -> xbox/mohaa/src/xbox_stdio.c
int sprintf_(char *s, const char *format, ...);
int snprintf_(char *s, size_t count, const char *format, ...);
int vsprintf_(char *s, const char *format, va_list arg);
int vsnprintf_(char *s, size_t count, const char *format, va_list arg);
int xbox_fprintf(FILE *stream, const char *format, ...);
int xbox_vfprintf(FILE *stream, const char *format, va_list arg);
int xbox_printf(const char *format, ...);
int xbox_vprintf(const char *format, va_list arg);
int xbox_sscanf(const char *str, const char *format, ...);
int xbox_vsscanf(const char *str, const char *format, va_list arg);
double xbox_strtod(const char *nptr, char **endptr);
float xbox_strtof(const char *nptr, char **endptr);
long double xbox_strtold(const char *nptr, char **endptr);
double xbox_atof(const char *nptr);

#ifdef __cplusplus
}
#endif

#define fopen     xbox_fopen
#define fseek     xbox_fseek
#define fread     xbox_fread
#define fwrite    xbox_fwrite
#define sprintf   sprintf_
#define snprintf  snprintf_
#define vsprintf  vsprintf_
#define vsnprintf vsnprintf_
#define fprintf   xbox_fprintf
#define vfprintf  xbox_vfprintf
#define printf    xbox_printf
#define vprintf   xbox_vprintf
#define sscanf    xbox_sscanf
#define vsscanf   xbox_vsscanf
#define strtod    xbox_strtod
#define strtof    xbox_strtof
#define strtold   xbox_strtold
#define atof      xbox_atof

// nxdk-cc compiles with -ffreestanding -fno-builtin, so these were real calls
// into the libc instead of one instruction. Included here so that the headers
// declare the functions before these macros exist (and after the ones above,
// which they have to see: std::strtof is std::xbox_strtof).
#ifdef __cplusplus
#include <cmath>
#include <cstring>
#include <utility> // uses std::memcpy
#else
#include <math.h>
#endif
#define fabs(x)  __builtin_fabs(x)
#define fabsf(x) __builtin_fabsf(x)
#define sqrt(x)  __builtin_sqrt(x)
#define sqrtf(x) __builtin_sqrtf(x)
// The same for fixed-size copies: VectorCopy-like memcpy calls become moves, and
// anything else still ends in the functions of xbox_string.c or the libc.
#define memcpy(d, s, n) __builtin_memcpy(d, s, n)
#define memset(d, c, n) __builtin_memset(d, c, n)
#define memcmp(a, b, n) __builtin_memcmp(a, b, n)
#define strlen(s)       __builtin_strlen(s)
