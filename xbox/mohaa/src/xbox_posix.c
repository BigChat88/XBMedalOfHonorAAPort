/*
 * xbox_posix.c -- POSIX/CRT functions OpenMoHAA uses that nxdk's libc
 * (pdclib + xboxrt) does not provide, plus an fopen that fixes path
 * separators. Implemented on top of nxdk's winapi.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include <windows.h>

#include <io.h>
#include <sys/stat.h>

const char *Sys_Xbox_NativePath(const char *path, char *out, size_t size);

#define NATIVE(p) Sys_Xbox_NativePath((p), native, sizeof(native))

// compat/xbox_compat.h redirects fopen here: the engine and unzip.c (pk3
// reading) build paths with '/', which the Xbox kernel rejects.
#undef fopen
FILE *xbox_fopen(const char *path, const char *mode)
{
    char native[MAX_PATH];
    return fopen(NATIVE(path), mode);
}

// nxdk's ReadFile fails (instead of returning 0 bytes) at end of file, so
// pdclib sets the sticky error flag rather than EOF, and every later fread on
// that stream returns 0 even after rewinding. Code that reads a file to the
// end and seeks back (the MP3 codec scans whole files first) needs the flags
// cleared after a successful seek.
#undef fseek
int xbox_fseek(FILE *stream, long offset, int whence)
{
    int rc = fseek(stream, offset, whence);
    if (rc == 0)
        clearerr(stream);
    return rc;
}

// pdclib's fread copies a byte at a time from a 1 KB buffer, with one ReadFile
// per KB: a 16 KB block of a pk3 took 16 trips through the kernel's file system.
// Most of a level load was spent there. This keeps the same stream state, but
// whatever does not fit in the buffer goes straight to the destination.
#undef fread
size_t xbox_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    const size_t total = size * nmemb;
    char        *dest  = (char *)ptr;
    size_t       done  = 0;

    if (!total || stream->ungetidx || stream->bufidx > stream->bufend
        || (stream->status & (_PDCLIB_FWRITE | _PDCLIB_FAPPEND | _PDCLIB_ERRORFLAG | _PDCLIB_WIDESTREAM | _PDCLIB_EOFFLAG))
        || !(stream->status & (_PDCLIB_FREAD | _PDCLIB_FRW))) {
        // nothing to gain, and pdclib knows what to answer
        return fread(ptr, size, nmemb, stream);
    }

    mtx_lock(&stream->mtx);
    stream->status |= _PDCLIB_FREAD | _PDCLIB_BYTESTREAM;

    while (done < total) {
        const size_t buffered = stream->bufend - stream->bufidx;
        const size_t left     = total - done;
        DWORD        got      = 0;
        BOOL         ok;

        if (buffered) {
            const size_t n = buffered < left ? buffered : left;
            memcpy(dest + done, stream->buffer + stream->bufidx, n);
            stream->bufidx += n;
            done += n;
            continue;
        }

        if (left >= stream->bufsize) {
            ok = ReadFile(stream->handle, dest + done, (DWORD)left, &got, NULL);
            done += got;
        } else {
            ok = ReadFile(stream->handle, stream->buffer, (DWORD)stream->bufsize, &got, NULL);
            stream->bufidx = 0;
            stream->bufend = got;
        }
        stream->pos.offset += got;

        if (!ok) {
            errno = EIO;
            stream->status |= _PDCLIB_ERRORFLAG;
            break;
        }
        if (!got) {
            stream->status |= _PDCLIB_EOFFLAG;
            break;
        }
    }

    mtx_unlock(&stream->mtx);
    return done / size;
}

// The same for writing: pdclib's fwrite copies a byte at a time to a 1 KB buffer
// with one WriteFile per KB, and each of those grows the file on the hard disk.
// Nearly all of the 5 s that a saved game stalled the game was spent there.
#undef fwrite
size_t xbox_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    const size_t total = size * nmemb;
    const char  *src   = (const char *)ptr;
    size_t       done  = 0;

    if (total < stream->bufsize || stream->ungetidx || stream->bufidx < stream->bufend
        || (stream->status & (_PDCLIB_FREAD | _PDCLIB_FRW | _PDCLIB_FAPPEND | _PDCLIB_ERRORFLAG | _PDCLIB_WIDESTREAM | _PDCLIB_EOFFLAG))
        || !(stream->status & _PDCLIB_FWRITE)) {
        // nothing to gain, and pdclib knows what to answer
        return fwrite(ptr, size, nmemb, stream);
    }

    // what earlier small writes left in the buffer goes first
    if (fflush(stream) == EOF) {
        return 0;
    }

    mtx_lock(&stream->mtx);
    stream->status |= _PDCLIB_BYTESTREAM;

    while (done < total) {
        DWORD      put = 0;
        const BOOL ok  = WriteFile(stream->handle, src + done, (DWORD)(total - done), &put, NULL);

        done += put;
        stream->pos.offset += put;

        if (!ok || !put) {
            errno = EIO;
            stream->status |= _PDCLIB_ERRORFLAG;
            break;
        }
    }

    mtx_unlock(&stream->mtx);
    return done / size;
}

static void filetime_to_time(const FILETIME *ft, time_t *t)
{
    // FILETIME: 100 ns intervals since 1601-01-01
    ULARGE_INTEGER u;
    u.LowPart = ft->dwLowDateTime;
    u.HighPart = ft->dwHighDateTime;
    *t = (time_t)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

int stat(const char *path, struct stat *st)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    char                      native[MAX_PATH];

    if (!GetFileAttributesExA(NATIVE(path), GetFileExInfoStandard, &fad)) {
        errno = ENOENT;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? S_IFDIR : S_IFREG;
    st->st_size = (long)fad.nFileSizeLow;
    filetime_to_time(&fad.ftLastWriteTime, &st->st_mtime);
    filetime_to_time(&fad.ftCreationTime, &st->st_ctime);
    filetime_to_time(&fad.ftLastAccessTime, &st->st_atime);
    return 0;
}

int mkdir(const char *path, int mode)
{
    char native[MAX_PATH];

    (void)mode;
    if (!CreateDirectoryA(NATIVE(path), NULL)) {
        errno = GetLastError() == ERROR_ALREADY_EXISTS ? EEXIST : ENOENT;
        return -1;
    }
    return 0;
}

int rmdir(const char *path)
{
    char native[MAX_PATH];

    if (!RemoveDirectoryA(NATIVE(path))) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int access(const char *path, int mode)
{
    char native[MAX_PATH];

    (void)mode;
    if (GetFileAttributesA(NATIVE(path)) == INVALID_FILE_ATTRIBUTES) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int unlink(const char *path)
{
    char native[MAX_PATH];

    if (!DeleteFileA(NATIVE(path))) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

// There is no process environment on the Xbox; the only user (cl_main) is informative.
int putenv(char *string)
{
    (void)string;
    return 0;
}

// Single-threaded: pdclib's static buffer is safe to use.
struct tm *gmtime_r(const time_t *timer, struct tm *result)
{
    struct tm *t = gmtime(timer);
    if (!t)
        return NULL;
    *result = *t;
    return result;
}

struct tm *localtime_r(const time_t *timer, struct tm *result)
{
    struct tm *t = localtime(timer);
    if (!t)
        return NULL;
    *result = *t;
    return result;
}
