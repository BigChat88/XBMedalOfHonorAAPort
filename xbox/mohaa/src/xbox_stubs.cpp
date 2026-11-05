/*
 * xbox_stubs.cpp -- parts of OpenMoHAA that do not apply to the Xbox:
 *   - update checker (HTTP, sys/sys_update_checker.cpp)
 *   - libcurl (sys/sys_curl.c)
 *   - Windows-specific functions from sys/new/sys_win32_new.c
 *   - GameSpy: server browser and CD key (gamespy/serverbrowsing, gcdkey)
 *
 * The GameSpy ones are only used by the Internet multiplayer menus.
 */
#include "../../../third_party/openmohaa/code/qcommon/q_shared.h"
#include "../../../third_party/openmohaa/code/qcommon/qcommon.h"
#include "../../../third_party/openmohaa/code/sys/sys_update_checker.h"

#include <stdlib.h>
#include <xboxkrnl/xboxkrnl.h>

// ---------------------------------------------------------------------------
// Update checker

UpdateChecker updateChecker;

UpdateChecker::UpdateChecker()
    : lastMajor(0), lastMinor(0), lastPatch(0), versionChecked(false), thread(nullptr)
{
}
UpdateChecker::~UpdateChecker() {}
void UpdateChecker::Init() {}
void UpdateChecker::Process() {}
void UpdateChecker::Shutdown() {}
bool UpdateChecker::CheckNewVersion() const { return false; }
bool UpdateChecker::CheckNewVersion(int &major, int &minor, int &patch) const
{
    major = minor = patch = 0;
    return false;
}
void UpdateChecker::SetLatestVersion(int, int, int) {}

extern "C" {

void Sys_UpdateChecker_Init(void) {}
void Sys_UpdateChecker_Process(void) {}
void Sys_UpdateChecker_Shutdown(void) {}

// ---------------------------------------------------------------------------
// curl

void Sys_InitCurl(void) {}
void Sys_ShutdownCurl(void) {}

// ---------------------------------------------------------------------------
// sys_win32_new.c

void Sys_DebugPrint(const char *message)
{
    DbgPrint("%s", message);
}

void Sys_PumpMessageLoop(void) {}
void SetNormalThreadPriority(void) {}
void SetBelowNormalThreadPriority(void) {}

// ---------------------------------------------------------------------------
// GameSpy (cdecl: returning 0 is safe even if the real signature takes arguments)

int __GSIACResult;

void *gsimalloc(size_t size) { return malloc(size); }
void gsifree(void *ptr) { free(ptr); }
unsigned long current_time(void) { return (unsigned long)Sys_Milliseconds(); }

int SocketStartUp(void) { return 0; }
void SocketShutDown(void) {}

int gcd_init(void) { return 0; }
void gcd_shutdown(void) {}
void gcd_think(void) {}
void gcd_disconnect_user(void) {}
void gcd_authenticate_user(void) {}
void gcd_compute_response(void) {}

void *ServerListNew(void) { return NULL; }
void ServerListFree(void) {}
int ServerListUpdate(void) { return 0; }
int ServerListUpdate2(void) { return 0; }
int ServerListLANUpdate(void) { return 0; }
int ServerListAuxUpdate(void) { return 0; }
int ServerListThink(void) { return 0; }
int ServerListHalt(void) { return 0; }
int ServerListClear(void) { return 0; }
int ServerListCount(void) { return 0; }
int ServerListState(void) { return 0; }
char *ServerGetAddress(void) { return (char *)"0.0.0.0"; }
int ServerGetQueryPort(void) { return 0; }
int ServerGetPing(void) { return 0; }
int ServerGetIntValue(void) { return 0; }
char *ServerGetStringValue(void) { return (char *)""; }

} // extern "C"
