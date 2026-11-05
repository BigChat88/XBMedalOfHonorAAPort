/*
 * sys_xbox.c -- Xbox (nxdk) system layer. Replaces sys/sys_win32.c.
 *
 * Paths:
 *   D:\           game data (DVD or the XBE's folder): D:\main\Pak*.pk3
 *   E:\OGXMOHAA\  config and saved games (hard disk)
 *
 * Modules: cgame and game are linked statically (with their symbols renamed),
 * so Sys_LoadLibrary/Sys_LoadFunction resolve against a fixed table instead of
 * loading DLLs.
 */
#include "../../../third_party/openmohaa/code/qcommon/q_shared.h"
#include "../../../third_party/openmohaa/code/qcommon/qcommon.h"
#include "../../../third_party/openmohaa/code/sys/sys_local.h"

#include <windows.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <nxdk/mount.h>
#include <xboxkrnl/xboxkrnl.h>

#define XBOX_HOME_DRIVE 'E'
#define XBOX_HOME_PATH  "E:\\OGXMOHAA"

/*
==============================================================
EARLY STARTUP
==============================================================
*/

// Runs before the global C++ constructors (.CRT$XCU), so the screen shows that
// the XBE started even if a constructor crashes.
static void __cdecl Xbox_EarlyInit(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugPrint("OGX MOHAA: XBE started, running constructors...\n");
}
__attribute__((section(".CRT$XCB"), used)) static void(__cdecl *xbox_earlyInit)(void) = Xbox_EarlyInit;

/*
==============================================================
STATIC MODULES
==============================================================
*/

void *GetGameAPI(void *import);
void *GetCGameAPI(void *import);

/*
Each module (cgame, game) gets the life cycle of a DLL, which the engine
expects: it frees all of a module's memory when it unloads it (Z_FreeTags) and
loads it again for every map or saved game, so the module's statics must start
over each time. tools/prefix-module.sh moves each module's data and BSS to
sections of its own (.g_data/.g_bss for game), its static constructors to
.g_i$U (so the CRT doesn't run them at boot) and its atexit calls (the static
destructors) to g_module_atexit. Loading a module restores its data from a copy
taken before its first load, zeroes its BSS and runs its constructors;
unloading it runs its destructors.
*/
typedef void(__cdecl *moduleFunc_t)(void);

#define MODULE_MAX_ATEXIT 1024

typedef struct {
    moduleFunc_t funcs[MODULE_MAX_ATEXIT];
    int          count;
} moduleAtexit_t;

static moduleAtexit_t cgameAtexit, gameAtexit;

static int Sys_Xbox_ModuleAtexit(moduleAtexit_t *list, moduleFunc_t func)
{
    if (list->count >= MODULE_MAX_ATEXIT)
        return -1;
    list->funcs[list->count++] = func;
    return 0;
}

// What the modules call instead of atexit (see tools/prefix-module.sh)
int cg_module_atexit(moduleFunc_t func) { return Sys_Xbox_ModuleAtexit(&cgameAtexit, func); }
int g_module_atexit(moduleFunc_t func) { return Sys_Xbox_ModuleAtexit(&gameAtexit, func); }

// Bounds of each module's constructor table: the linker sorts the sections by
// the text after '$', so the module's .g_i$U entries end up between these.
__attribute__((section(".cg_i$A"), used)) static moduleFunc_t const cgameInitStart = NULL;
__attribute__((section(".cg_i$Z"), used)) static moduleFunc_t const cgameInitEnd = NULL;
__attribute__((section(".g_i$A"), used)) static moduleFunc_t const gameInitStart = NULL;
__attribute__((section(".g_i$Z"), used)) static moduleFunc_t const gameInitEnd = NULL;

typedef struct {
    const char      *library; // prefix of the requested name (e.g. "game_mohaa.dll")
    const char      *symbol;
    void            *address;
    const char      *dataSection;
    const char      *bssSection;
    const moduleFunc_t *initStart, *initEnd;
    moduleAtexit_t  *atexitList;
} staticSymbol_t;

static const staticSymbol_t staticSymbols[] = {
    {"cgame", "GetCGameAPI", (void *)GetCGameAPI, ".cg_data", ".cg_bss", &cgameInitStart, &cgameInitEnd, &cgameAtexit},
    {"game",  "GetGameAPI",  (void *)GetGameAPI,  ".g_data",  ".g_bss",  &gameInitStart,  &gameInitEnd,  &gameAtexit },
};

typedef struct {
    qboolean loaded;
    qboolean started;  // the data copy exists
    byte    *data, *bss;
    uint32_t dataSize, bssSize;
    byte    *dataCopy; // the module's data as the XBE loaded it
} moduleState_t;

static moduleState_t moduleStates[ARRAY_LEN(staticSymbols)];

// Finds a section of the running XBE by name.
static qboolean Sys_Xbox_FindSection(const char *name, byte **start, uint32_t *size)
{
    const byte    *xbe     = (const byte *)0x00010000;
    const uint32_t count   = *(const uint32_t *)(xbe + 0x11C);
    const byte    *headers = (const byte *)*(const uint32_t *)(xbe + 0x120);

    for (uint32_t i = 0; i < count; i++) {
        const byte *h = headers + i * 56;
        if (!strcmp((const char *)*(const uint32_t *)(h + 20), name)) {
            *start = (byte *)*(const uint32_t *)(h + 4);
            *size  = *(const uint32_t *)(h + 8);
            return qtrue;
        }
    }
    *start = NULL;
    *size  = 0;
    return qfalse;
}

static void Sys_Xbox_StartModule(int index)
{
    const staticSymbol_t *def = &staticSymbols[index];
    moduleState_t        *m   = &moduleStates[index];

    if (!m->started) {
        // first load: nothing has touched the module's data yet
        if (!Sys_Xbox_FindSection(def->dataSection, &m->data, &m->dataSize))
            Com_Printf("WARNING: section %s not found\n", def->dataSection);
        if (!Sys_Xbox_FindSection(def->bssSection, &m->bss, &m->bssSize))
            Com_Printf("WARNING: section %s not found\n", def->bssSection);
        m->dataCopy = (byte *)malloc(m->dataSize);
        if (m->dataCopy)
            memcpy(m->dataCopy, m->data, m->dataSize);
        m->started = qtrue;
    } else {
        if (m->dataCopy)
            memcpy(m->data, m->dataCopy, m->dataSize);
        if (m->bss)
            memset(m->bss, 0, m->bssSize);
    }

    for (const moduleFunc_t *f = def->initStart + 1; f < def->initEnd; f++) {
        if (*f)
            (*f)();
    }
    m->loaded = qtrue;
}

static void Sys_Xbox_StopModule(int index)
{
    moduleAtexit_t *list = staticSymbols[index].atexitList;

    while (list->count > 0)
        list->funcs[--list->count]();
    moduleStates[index].loaded = qfalse;
}

// The "handle" is the library's index in the table plus one.
void *Sys_Xbox_LoadLibrary(const char *name)
{
    const char *base = strrchr(name, '\\');
    const char *slash = strrchr(name, '/');
    if (slash > base)
        base = slash;
    base = base ? base + 1 : name;

    for (size_t i = 0; i < ARRAY_LEN(staticSymbols); i++) {
        if (!Q_stricmpn(base, staticSymbols[i].library, strlen(staticSymbols[i].library))) {
            if (!moduleStates[i].loaded)
                Sys_Xbox_StartModule(i);
            return (void *)(intptr_t)(i + 1);
        }
    }
    return NULL;
}

void *Sys_Xbox_LoadFunction(void *handle, const char *symbol)
{
    intptr_t i = (intptr_t)handle - 1;
    if (i < 0 || i >= (intptr_t)ARRAY_LEN(staticSymbols))
        return NULL;
    if (strcmp(staticSymbols[i].symbol, symbol))
        return NULL;
    return staticSymbols[i].address;
}

void Sys_Xbox_UnloadLibrary(void *handle)
{
    intptr_t i = (intptr_t)handle - 1;
    if (i >= 0 && i < (intptr_t)ARRAY_LEN(staticSymbols) && moduleStates[i].loaded)
        Sys_Xbox_StopModule(i);
}

const char *Sys_Xbox_LibraryError(void)
{
    return "static module not found";
}

/*
==============================================================
TIME AND CPU
==============================================================
*/

void Sys_SetFloatEnv(void)
{
    // nxdk leaves the FPU at round-to-nearest with 64-bit precision; the
    // original game ran with 53 bits (_PC_53).
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw = (cw & ~0x0F00) | 0x0200; // PC=53 bits, RC=nearest
    __asm__ volatile("fldcw %0" : : "m"(cw));

    // float math is SSE: denormals are very slow and useless here, flush them
    // to zero (FTZ)
    {
        unsigned int csr;
        __asm__ volatile("stmxcsr %0" : "=m"(csr));
        csr |= 0x8000;
        __asm__ volatile("ldmxcsr %0" : : "m"(csr));
    }
}

int Sys_Milliseconds(void)
{
    static LARGE_INTEGER base, freq;
    LARGE_INTEGER        now;

    if (!freq.QuadPart) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&base);
    }
    QueryPerformanceCounter(&now);
    return (int)((now.QuadPart - base.QuadPart) * 1000 / freq.QuadPart);
}

void Sys_Sleep(int msec)
{
    if (msec > 0)
        Sleep(msec);
}

qboolean Sys_RandomBytes(byte *string, int len)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    unsigned int seed = (unsigned int)t.LowPart ^ (unsigned int)t.HighPart;
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        string[i] = (byte)(seed >> 16);
    }
    return qtrue;
}

qboolean Sys_LowPhysicalMemory(void)
{
    MM_STATISTICS ms;
    ms.Length = sizeof(ms);
    MmQueryStatistics(&ms);
    return ms.TotalPhysicalPages * 4096u <= 96u * 1024 * 1024;
}

/*
==============================================================
PATHS
==============================================================
*/

// The Xbox kernel (NT family) only accepts backslashes, but the engine builds
// paths such as "D:/main/pak0.pk3". Every path that reaches the OS goes
// through here first. `out` may be the same buffer as `path`.
const char *Sys_Xbox_NativePath(const char *path, char *out, size_t size)
{
    size_t n = 0;
    for (; path[n] && n + 1 < size; n++)
        out[n] = path[n] == '/' ? '\\' : path[n];
    out[n] = 0;
    return out;
}

static char *Sys_DefaultHomePath(void)
{
    static char homePath[MAX_OSPATH] = XBOX_HOME_PATH;
    return homePath;
}

char *Sys_DefaultHomeConfigPath(void) { return Sys_DefaultHomePath(); }
char *Sys_DefaultHomeDataPath(void)   { return Sys_DefaultHomePath(); }
char *Sys_DefaultHomeStatePath(void)  { return Sys_DefaultHomePath(); }
char *Sys_SteamPath(void)             { return ""; }
char *Sys_GogPath(void)               { return ""; }
char *Sys_MicrosoftStorePath(void)    { return ""; }

char *Sys_GetCurrentUser(void)
{
    return "player";
}

const char *Sys_Basename(char *path)
{
    static char base[MAX_OSPATH];
    int         length;

    if (!path || !path[0])
        return "";

    length = (int)strlen(path) - 1;
    while (length > 0 && path[length] == '\\')
        length--;
    while (length > 0 && path[length - 1] != '\\')
        length--;

    Q_strncpyz(base, &path[length], sizeof(base));

    length = (int)strlen(base) - 1;
    while (length > 0 && base[length] == '\\')
        base[length--] = '\0';
    return base;
}

const char *Sys_Dirname(char *path)
{
    static char dir[MAX_OSPATH];
    int         length;

    // nxdk may call main() without argv[0]
    if (!path || !path[0])
        return "D:";

    Q_strncpyz(dir, path, sizeof(dir));
    length = (int)strlen(dir) - 1;
    while (length > 0 && dir[length] != '\\')
        length--;
    dir[length] = '\0';
    return dir;
}

FILE *Sys_FOpen(const char *ospath, const char *mode)
{
    char   native[MAX_OSPATH];
    size_t length = strlen(ospath);

    if (length == 0 || ospath[length - 1] == ' ' || ospath[length - 1] == '.')
        return NULL;
    return fopen(Sys_Xbox_NativePath(ospath, native, sizeof(native)), mode);
}

qboolean Sys_Mkdir(const char *path)
{
    char native[MAX_OSPATH];

    if (!CreateDirectoryA(Sys_Xbox_NativePath(path, native, sizeof(native)), NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return qfalse;
    return qtrue;
}

FILE *Sys_Mkfifo(const char *ospath)
{
    (void)ospath;
    return NULL;
}

char *Sys_Cwd(void)
{
    return "D:";
}

char *Sys_BinaryPathRelative(const char *relative)
{
    static char resolved[MAX_OSPATH];
    Com_sprintf(resolved, sizeof(resolved), "%s\\%s", Sys_BinaryPath(), relative);
    return resolved;
}

/*
==============================================================
DIRECTORY SCANNING
==============================================================
*/

#define MAX_FOUND_FILES 0x1000

static void Sys_ListFilteredFiles(const char *basedir, const char *subdirs, const char *filter,
                                  qboolean wantsubs, char **list, int *numfiles)
{
    char             search[MAX_OSPATH], newsubdirs[MAX_OSPATH], filename[MAX_OSPATH];
    WIN32_FIND_DATAA fd;
    HANDLE           h;

    if (*numfiles >= MAX_FOUND_FILES - 1 || !basedir[0])
        return;

    if (subdirs[0])
        Com_sprintf(search, sizeof(search), "%s\\%s\\*", basedir, subdirs);
    else
        Com_sprintf(search, sizeof(search), "%s\\*", basedir);

    h = FindFirstFileA(Sys_Xbox_NativePath(search, search, sizeof(search)), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;

    do {
        if (!Q_stricmp(fd.cFileName, ".") || !Q_stricmp(fd.cFileName, ".."))
            continue;

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wantsubs) {
            if (subdirs[0])
                Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s\\%s", subdirs, fd.cFileName);
            else
                Q_strncpyz(newsubdirs, fd.cFileName, sizeof(newsubdirs));
            Sys_ListFilteredFiles(basedir, newsubdirs, filter, wantsubs, list, numfiles);
        }

        if (*numfiles >= MAX_FOUND_FILES - 1)
            break;

        if (subdirs[0])
            Com_sprintf(filename, sizeof(filename), "%s\\%s", subdirs, fd.cFileName);
        else
            Q_strncpyz(filename, fd.cFileName, sizeof(filename));

        if (!Com_FilterPath(filter, filename, qfalse))
            continue;

        list[*numfiles] = CopyString(filename);
        for (char *c = strchr(list[*numfiles], '\\'); c; c = strchr(c, '\\'))
            *c = '/';
        (*numfiles)++;
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

static int Sys_SortStrings(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

char **Sys_ListFiles(const char *directory, const char *extension, const char *filter, int *numfiles,
                     qboolean wantsubs)
{
    char             search[MAX_OSPATH];
    char            *list[MAX_FOUND_FILES];
    char           **listCopy;
    char             buffer[64];
    int              nfiles = 0;
    WIN32_FIND_DATAA fd;
    HANDLE           h;
    qboolean         dirsOnly;

    *numfiles = 0;
    if (!directory[0])
        return NULL;
    if (!extension)
        extension = "";

    // a slash as the extension asks for directories; anything else for files
    dirsOnly = extension[0] == '/' && !extension[1];
    if (!filter && !dirsOnly) {
        Com_sprintf(buffer, sizeof(buffer), "*%s", extension);
        filter = buffer;
    }

    if (filter) {
        Sys_ListFilteredFiles(directory, "", filter, wantsubs, list, &nfiles);
    } else {
        Com_sprintf(search, sizeof(search), "%s\\*", directory);
        h = FindFirstFileA(Sys_Xbox_NativePath(search, search, sizeof(search)), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                if (!Q_stricmp(fd.cFileName, ".") || !Q_stricmp(fd.cFileName, ".."))
                    continue;
                if (nfiles >= MAX_FOUND_FILES - 1)
                    break;
                list[nfiles++] = CopyString(fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }

    if (!nfiles)
        return NULL;

    listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
    memcpy(listCopy, list, nfiles * sizeof(*listCopy));
    listCopy[nfiles] = NULL;
    if (!filter)
        qsort(listCopy, nfiles, sizeof(*listCopy), Sys_SortStrings);

    *numfiles = nfiles;
    return listCopy;
}

void Sys_FreeFileList(char **list)
{
    if (!list)
        return;
    for (int i = 0; list[i]; i++)
        Z_Free(list[i]);
    Z_Free(list);
}

/*
==============================================================
DIALOGS AND MISC
==============================================================
*/

void Xbox_ConsoleShowError(const char *error);

void Sys_ErrorDialog(const char *error)
{
    // No windows: take the screen back and show the error with the log.
    Xbox_ConsoleShowError(error);
    for (;;)
        Sleep(1000);
}

dialogResult_t Sys_Dialog(dialogType_t type, const char *message, const char *title)
{
    debugPrint("%s: %s\n", title, message);
    return type == DT_YES_NO ? DR_NO : DR_OK;
}

void Sys_GLimpSafeInit(void) {}
void Sys_GLimpInit(void) {}

// Logs what the kernel reports and how much the heap really hands out
// (128 MB consoles/xemu vs. 64 MB).
// Puts what was written to a file on the disk: the kernel caches writes and
// only writes them later, so a save could be lost by turning the console off
// (files.cpp, FS_FCloseFile).
void Sys_Xbox_FlushFile(FILE *file)
{
    IO_STATUS_BLOCK iosb;

    fflush(file);
    // pdclib's FILE starts with the kernel handle
    NtFlushBuffersFile(*(HANDLE *)file, &iosb);
}

static void Sys_Xbox_MemoryReport(void)
{
    MM_STATISTICS ms;
    static void  *blocks[160];
    int           n;

    ms.Length = sizeof(ms);
    MmQueryStatistics(&ms);

    for (n = 0; n < (int)(sizeof(blocks) / sizeof(blocks[0])); n++) {
        blocks[n] = malloc(1024 * 1024);
        if (!blocks[n])
            break;
    }
    for (int i = 0; i < n; i++)
        free(blocks[i]);

    Com_Printf("Xbox memory: %lu MB physical, %lu MB available, XBE init flags 0x%lx, malloc probe %d MB, "
               "NV2A RAM %lu MB\n",
               ms.TotalPhysicalPages / 256, ms.AvailablePages / 256,
               // the XBE header is mapped at 0x10000; init flags at +0x124
               (unsigned long)*(volatile ULONG *)0x00010124, n,
               // NV_PFB_CSTATUS: the RAM size the hardware (or xemu) reports
               (unsigned long)(*(volatile ULONG *)0xFD10020C >> 20));
}

void Sys_PlatformInit(void)
{
    debugPrint("OGX MOHAA: main() reached\n");
    Sys_SetFloatEnv();
    Sys_Xbox_MemoryReport();

    // nxdk mounts D: (the XBE's folder); the hard disk has to be mounted here.
    if (!nxIsDriveMounted(XBOX_HOME_DRIVE))
        nxMountDrive(XBOX_HOME_DRIVE, "\\Device\\Harddisk0\\Partition1\\");
    CreateDirectoryA(XBOX_HOME_PATH, NULL);

    Sys_PlatformInit_New();
}

void Sys_PlatformExit(void) {}

// Counterparts of sys/new/sys_win32_new.c
void Sys_PlatformInit_New(void) {}
void Sys_PrepareBackTrace(void) {}
void Sys_PrintBackTrace(void) {}

void Sys_SetEnv(const char *name, const char *value)
{
    (void)name;
    (void)value;
}

int Sys_PID(void)
{
    return 1;
}

qboolean Sys_PIDIsRunning(int pid)
{
    (void)pid;
    return qfalse;
}

qboolean Sys_DllExtension(const char *name)
{
    return COM_CompareExtension(name, DLL_EXT);
}

qboolean Sys_OpenFolderInPlatformFileManager(const char *path)
{
    (void)path;
    return qfalse;
}

qboolean Sys_SetMaxFileLimit(void)
{
    return qtrue;
}
