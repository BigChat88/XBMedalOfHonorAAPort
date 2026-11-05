/* ioapi.c -- IO base function header for compress/uncompress .zip
   files using zlib + zip or unzip API

   Version 1.01e, February 12th, 2005

   Copyright (C) 1998-2005 Gilles Vollant
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_INTERNAL_ZLIB
#include "zlib.h"
#else
#include <zlib.h>
#endif

#include "ioapi.h"



/* I've found an old Unix (a SunOS 4.1.3_U1) without all SEEK_* defined.... */

#ifndef SEEK_CUR
#define SEEK_CUR    1
#endif

#ifndef SEEK_END
#define SEEK_END    2
#endif

#ifndef SEEK_SET
#define SEEK_SET    0
#endif

voidpf ZCALLBACK fopen_file_func OF((
   voidpf opaque,
   const char* filename,
   int mode));

uLong ZCALLBACK fread_file_func OF((
   voidpf opaque,
   voidpf stream,
   void* buf,
   uLong size));

uLong ZCALLBACK fwrite_file_func OF((
   voidpf opaque,
   voidpf stream,
   const void* buf,
   uLong size));

long ZCALLBACK ftell_file_func OF((
   voidpf opaque,
   voidpf stream));

long ZCALLBACK fseek_file_func OF((
   voidpf opaque,
   voidpf stream,
   uLong offset,
   int origin));

int ZCALLBACK fclose_file_func OF((
   voidpf opaque,
   voidpf stream));

int ZCALLBACK ferror_file_func OF((
   voidpf opaque,
   voidpf stream));


voidpf ZCALLBACK fopen_file_func (opaque, filename, mode)
   voidpf opaque;
   const char* filename;
   int mode;
{
    FILE* file = NULL;
    const char* mode_fopen = NULL;
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER)==ZLIB_FILEFUNC_MODE_READ)
        mode_fopen = "rb";
    else
    if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
        mode_fopen = "r+b";
    else
    if (mode & ZLIB_FILEFUNC_MODE_CREATE)
        mode_fopen = "wb";

    if ((filename!=NULL) && (mode_fopen != NULL))
        file = fopen(filename, mode_fopen);
    return file;
}


#ifdef NXDK
/*
 * OGX MOHAA: FS_LoadZipFile goes through the directory of a pak twice, with a seek
 * and several small reads per file, and the paks hold tens of thousands of files.
 * That was a third of the time before the first image of the game. While a pak is
 * being listed, reads come from a window of the file instead.
 */
#define ZIP_WINDOW_SIZE (256 * 1024)
static unsigned char *zipWindow;
static FILE          *zipStream;
static long           zipPos, zipWindowStart, zipWindowBytes;

void Xbox_ZipWindowBegin(void)
{
    zipWindow = malloc(ZIP_WINDOW_SIZE);
    zipStream = NULL;
}

void Xbox_ZipWindowEnd(void)
{
    free(zipWindow);
    zipWindow = NULL;
    zipStream = NULL;
}

static void Xbox_ZipWindowUse(FILE *stream)
{
    if (stream != zipStream) {
        zipStream      = stream;
        zipPos         = ftell(stream);
        zipWindowStart = 0;
        zipWindowBytes = 0;
    }
}

static uLong Xbox_ZipWindowRead(FILE *stream, unsigned char *dest, uLong size)
{
    uLong done = 0;

    Xbox_ZipWindowUse(stream);
    while (done < size) {
        long inside = zipPos - zipWindowStart;

        if (inside < 0 || inside >= zipWindowBytes) {
            fseek(stream, zipPos, SEEK_SET);
            zipWindowStart = zipPos;
            zipWindowBytes = (long)fread(zipWindow, 1, ZIP_WINDOW_SIZE, stream);
            if (zipWindowBytes <= 0) {
                zipWindowBytes = 0;
                break;
            }
            inside = 0;
        }

        {
            uLong n = (uLong)(zipWindowBytes - inside);
            if (n > size - done) {
                n = size - done;
            }
            memcpy(dest + done, zipWindow + inside, n);
            done += n;
            zipPos += (long)n;
        }
    }
    return done;
}
#endif

uLong ZCALLBACK fread_file_func (opaque, stream, buf, size)
   voidpf opaque;
   voidpf stream;
   void* buf;
   uLong size;
{
    uLong ret;
#ifdef NXDK
    if (zipWindow) {
        return Xbox_ZipWindowRead((FILE *)stream, (unsigned char *)buf, size);
    }
#endif
    ret = (uLong)fread(buf, 1, (size_t)size, (FILE *)stream);
    return ret;
}


uLong ZCALLBACK fwrite_file_func (opaque, stream, buf, size)
   voidpf opaque;
   voidpf stream;
   const void* buf;
   uLong size;
{
    uLong ret;
    ret = (uLong)fwrite(buf, 1, (size_t)size, (FILE *)stream);
    return ret;
}

long ZCALLBACK ftell_file_func (opaque, stream)
   voidpf opaque;
   voidpf stream;
{
    long ret;
#ifdef NXDK
    if (zipWindow) {
        Xbox_ZipWindowUse((FILE *)stream);
        return zipPos;
    }
#endif
    ret = ftell((FILE *)stream);
    return ret;
}

long ZCALLBACK fseek_file_func (opaque, stream, offset, origin)
   voidpf opaque;
   voidpf stream;
   uLong offset;
   int origin;
{
    int fseek_origin=0;
    long ret;
#ifdef NXDK
    if (zipWindow) {
        Xbox_ZipWindowUse((FILE *)stream);
        if (origin == ZLIB_FILEFUNC_SEEK_SET) {
            zipPos = (long)offset;
            return 0;
        }
        if (origin == ZLIB_FILEFUNC_SEEK_CUR) {
            zipPos += (long)offset;
            return 0;
        }
        // from the end: the file knows its size
        fseek((FILE *)stream, (long)offset, SEEK_END);
        zipPos = ftell((FILE *)stream);
        return 0;
    }
#endif
    switch (origin)
    {
    case ZLIB_FILEFUNC_SEEK_CUR :
        fseek_origin = SEEK_CUR;
        break;
    case ZLIB_FILEFUNC_SEEK_END :
        fseek_origin = SEEK_END;
        break;
    case ZLIB_FILEFUNC_SEEK_SET :
        fseek_origin = SEEK_SET;
        break;
    default: return -1;
    }
    ret = 0;
    fseek((FILE *)stream, offset, fseek_origin);
    return ret;
}

int ZCALLBACK fclose_file_func (opaque, stream)
   voidpf opaque;
   voidpf stream;
{
    int ret;
    ret = fclose((FILE *)stream);
    return ret;
}

int ZCALLBACK ferror_file_func (opaque, stream)
   voidpf opaque;
   voidpf stream;
{
    int ret;
    ret = ferror((FILE *)stream);
    return ret;
}

void fill_fopen_filefunc (pzlib_filefunc_def)
  zlib_filefunc_def* pzlib_filefunc_def;
{
    pzlib_filefunc_def->zopen_file = fopen_file_func;
    pzlib_filefunc_def->zread_file = fread_file_func;
    pzlib_filefunc_def->zwrite_file = fwrite_file_func;
    pzlib_filefunc_def->ztell_file = ftell_file_func;
    pzlib_filefunc_def->zseek_file = fseek_file_func;
    pzlib_filefunc_def->zclose_file = fclose_file_func;
    pzlib_filefunc_def->zerror_file = ferror_file_func;
    pzlib_filefunc_def->opaque = NULL;
}
