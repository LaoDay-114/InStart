// ============================================================
// InStart DLL 入口：DllMain 只派发线程，所有初始化在独立线程完成
// ============================================================
#include <windows.h>
#include "hooks.h"
#include "config.h"

// 全局配置/状态实例
InStartConfig g_cfg;
InStartState  g_state;

static DWORD WINAPI init_thread(LPVOID arg) {
    config_init_path(arg); // 创建 InStartConfig 文件夹（首次启动）
    config_load();         // 读取已保存的配置
    hooks_init((HMODULE)arg);
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
