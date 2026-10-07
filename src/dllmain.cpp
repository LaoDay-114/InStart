#include <windows.h>
#include "hooks.h"
#include "config.h"
#include "update_check.h"

InStartConfig g_cfg;
InStartState  g_state;

static DWORD WINAPI init_thread(LPVOID arg) {
    config_init_path(arg);
    config_load();
    hooks_init((HMODULE)arg);
    update_check_async();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        HANDLE th = CreateThread(nullptr, 0, init_thread, self, 0, nullptr);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
