/*
 * con_xbox.c -- Xbox system console. Replaces sys/con_passive.c.
 *
 * Until the renderer takes over the screen, the engine log is printed with
 * nxdk's debugPrint. GLimp_Init calls Xbox_ConsoleRelease() before starting
 * pbgl. On a fatal error, Xbox_ConsoleShowError() takes the screen back and
 * shows the last log lines together with the error.
 *
 * Every line also goes to COM1 of the SuperIO chip (0x3F8). xemu can write
 * that port to a file on the host:
 *   xemu -device lpc47m157 -serial file:serial.log
 *   (scripts/run-xemu.ps1 -SerialLog ...)
 * Real consoles without a debug serial port just ignore the writes.
 */
#include "../../../third_party/openmohaa/code/qcommon/q_shared.h"
#include "../../../third_party/openmohaa/code/qcommon/qcommon.h"
#include "../../../third_party/openmohaa/code/sys/sys_local.h"

#include <hal/debug.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>

#define HISTORY_LINES 24
#define LINE_CHARS    100

static qboolean onScreen;
static char     history[HISTORY_LINES][LINE_CHARS];
static int      historyHead;
static int      historyCol;

#define COM1_BASE 0x3F8

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

#define SUPERIO_CONFIG 0x2E
#define SUPERIO_DATA   0x2F

static void SuperIO_Write(unsigned char reg, unsigned char value)
{
    outb(SUPERIO_CONFIG, reg);
    outb(SUPERIO_DATA, value);
}

// The LPC47M157 SuperIO starts with its UARTs disabled: map serial port A
// (logical device 4) to 0x3F8, enable it, then set 8N1.
static void Serial_Init(void)
{
    outb(SUPERIO_CONFIG, 0x55); // enter configuration mode
    SuperIO_Write(0x07, 0x04);  // select logical device 4 (serial A)
    SuperIO_Write(0x60, COM1_BASE >> 8);
    SuperIO_Write(0x61, COM1_BASE & 0xFF);
    SuperIO_Write(0x30, 0x01);  // activate
    outb(SUPERIO_CONFIG, 0xAA); // leave configuration mode

    outb(COM1_BASE + 3, 0x80);  // DLAB on
    outb(COM1_BASE + 0, 0x01);  // 115200 baud
    outb(COM1_BASE + 1, 0x00);
    outb(COM1_BASE + 3, 0x03);  // 8N1, DLAB off
    outb(COM1_BASE + 2, 0x07);  // enable and clear FIFOs
}

static void Serial_Write(const char *text)
{
    for (; *text; text++) {
        if (*text == '\n')
            Serial_Write("\r");
        // Wait (bounded) for the transmit holding register to empty.
        for (int spin = 0; spin < 10000 && !(inb(COM1_BASE + 5) & 0x20); spin++)
            ;
        outb(COM1_BASE, (unsigned char)*text);
    }
}

static void Con_StripColors(const char *in, char *out, size_t size)
{
    size_t n = 0;
    while (*in && n + 1 < size) {
        if (Q_IsColorString(in)) {
            in += 2;
            continue;
        }
        out[n++] = *in++;
    }
    out[n] = 0;
}

static void Con_AddHistory(const char *msg)
{
    for (; *msg; msg++) {
        if (*msg == '\n' || historyCol >= LINE_CHARS - 1) {
            history[historyHead][historyCol] = 0;
            historyHead = (historyHead + 1) % HISTORY_LINES;
            historyCol = 0;
            history[historyHead][0] = 0;
            if (*msg == '\n')
                continue;
        }
        history[historyHead][historyCol++] = *msg;
        history[historyHead][historyCol] = 0;
    }
}

void CON_Init(void)
{
    Serial_Init();
    Serial_Write("---- OGX MOHAA serial log ----\n");
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugClearScreen();
    onScreen = qtrue;
}

void CON_Shutdown(void) {}

char *CON_Input(void)
{
    return NULL;
}

// With "set xbox_memtrace 1" every serial log line starts with the free
// physical memory, so a log shows what each loading step costs.
static void Serial_WriteMemTrace(const char *text)
{
    static qboolean lineStart = qtrue;
    char            prefix[24];

    for (const char *p = text; *p;) {
        if (lineStart) {
            MM_STATISTICS ms;
            ms.Length = sizeof(ms);
            MmQueryStatistics(&ms);
            Com_sprintf(prefix, sizeof(prefix), "[%6lu KB] ", ms.AvailablePages * 4);
            Serial_Write(prefix);
            lineStart = qfalse;
        }
        const char *nl  = strchr(p, '\n');
        size_t      len = nl ? (size_t)(nl - p + 1) : strlen(p);
        char        part[MAXPRINTMSG];
        Q_strncpyz(part, p, len + 1 < sizeof(part) ? len + 1 : sizeof(part));
        Serial_Write(part);
        p += len;
        if (nl)
            lineStart = qtrue;
    }
}

void CON_Print(const char *msg)
{
    char clean[MAXPRINTMSG];

    Con_StripColors(msg, clean, sizeof(clean));
    Con_AddHistory(clean);
    // No DbgPrint here: with COM1 enabled the kernel debugger output goes to the
    // same port, so every line would show up twice in the serial log.
    if (Cvar_VariableIntegerValue("xbox_memtrace"))
        Serial_WriteMemTrace(clean);
    else
        Serial_Write(clean);
    if (onScreen)
        debugPrint("%s", clean);
}

// The renderer is about to use the framebuffer: stop writing to it.
void Xbox_ConsoleRelease(void)
{
    onScreen = qfalse;
}

void Xbox_ConsoleShowError(const char *error)
{
    Serial_Write("\nFATAL ERROR: ");
    Serial_Write(error);
    Serial_Write("\n");
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugClearScreen();
    onScreen = qtrue;

    debugPrint("---- last log lines ----\n");
    for (int i = 1; i <= HISTORY_LINES; i++) {
        const char *line = history[(historyHead + i) % HISTORY_LINES];
        if (line[0])
            debugPrint("%s\n", line);
    }
    debugPrint("\nFATAL ERROR: %s\n", error);
}
