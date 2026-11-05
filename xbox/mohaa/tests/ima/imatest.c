/* Native test of xbox_imastream.c: decodes an .ima file to raw PCM, reading it
 * through the same stream functions as the game, with small requests.
 * Build and run: see run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;
typedef int qboolean;
typedef int fileHandle_t;
#define qtrue 1
#define qfalse 0
#define MAX_QPATH 256
#define FS_SEEK_SET 0

typedef struct snd_info_s { int rate; float width; int channels; int samples; int size; int dataofs; int dataalign; } snd_info_t;
typedef struct snd_codec_s snd_codec_t;
typedef struct snd_stream_s { snd_codec_t *codec; fileHandle_t file; snd_info_t info; int length; int pos; void *ptr; } snd_stream_t;
typedef void *(*CODEC_LOAD)(const char *, snd_info_t *);
typedef snd_stream_t *(*CODEC_OPEN)(const char *);
typedef int (*CODEC_READ)(snd_stream_t *, int, void *);
typedef void (*CODEC_CLOSE)(snd_stream_t *);
struct snd_codec_s { char *ext; CODEC_LOAD load; CODEC_OPEN open; CODEC_READ read; CODEC_CLOSE close; snd_codec_t *next; };
snd_stream_t *S_IMA_CodecOpenStream(const char *filename);

static FILE *f;
static const char *path;
static int FS_Read(void *b, int n, int h) { (void)h; return (int)fread(b, 1, n, f); }
static int FS_Seek(int h, long o, int w) { (void)h; (void)w; return fseek(f, o, SEEK_SET); }
static int FS_FOpenFileRead(const char *n, fileHandle_t *h, int a, int b) { (void)n; (void)h; (void)a; (void)b; return 1; }
static void *Z_Malloc(int n) { return calloc(1, n); }
static void Z_Free(void *p) { free(p); }
static void COM_StripExtension(const char *a, char *b, int n) { (void)a; (void)n; b[0] = 0; }
static void Q_strcat(char *a, int n, const char *b) { (void)a; (void)n; (void)b; }
static void Com_Printf(const char *s, ...) { fputs(s, stderr); }
static snd_stream_t *S_CodecUtilOpen(const char *n, snd_codec_t *c) {
    snd_stream_t *s = calloc(1, sizeof(*s));
    (void)n;
    f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); s->length = (int)ftell(f); fseek(f, 0, SEEK_SET);
    s->codec = c;
    return s;
}
static void S_CodecUtilClose(snd_stream_t **s) { fclose(f); free(*s); *s = NULL; }

#include "imastream_body.inc"

int main(int argc, char **argv) {
    static char buf[4096];
    int n, total = 0;
    (void)argc;
    path = argv[1];
    snd_stream_t *s = S_IMA_CodecOpenStream("x");
    if (!s) return 1;
    FILE *out = fopen(argv[2], "wb");
    /* an odd size that crosses block boundaries */
    while ((n = s->codec->read(s, 3172, buf)) > 0) { fwrite(buf, 1, n, out); total += n; }
    fprintf(stderr, "rate %d channels %d samples %d size %d read %d\n", s->info.rate, s->info.channels, s->info.samples, s->info.size, total);
    fclose(out);
    s->codec->close(s);
    return 0;
}
