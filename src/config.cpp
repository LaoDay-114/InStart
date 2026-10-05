// ============================================================
// InStart 配置持久化：
//   首次启动在 DLL 同目录创建 InStartConfig 文件夹，
//   配置保存到 InStartConfig\config.txt（二进制整个结构体）
// ============================================================
#include <windows.h>
#include <cstdio>
#include "config.h"

static wchar_t g_cfgPath[MAX_PATH] = {};

void config_init_path(void* moduleHandle) {
    wchar_t dir[MAX_PATH] = {};
    if (!GetModuleFileNameW((HMODULE)moduleHandle, dir, MAX_PATH)) return;
    wchar_t* p = wcsrchr(dir, L'\\');
    if (!p) return;
    *p = L'\0';
    wcscat_s(dir, L"\\InStartConfig");
    CreateDirectoryW(dir, nullptr); // 已存在则忽略
    _snwprintf_s(g_cfgPath, MAX_PATH, L"%s\\config.txt", dir);
}

void config_load() {
    if (!g_cfgPath[0]) return;
    FILE* f = _wfopen(g_cfgPath, L"rb");
    if (!f) return; // 首次启动无配置文件，保持默认值
    // 结构体已扩展时旧配置文件读不满，fread 返回 0，保持默认值
    if (fread(&g_cfg, sizeof(g_cfg), 1, f) != 1) {
        fclose(f);
        return;
    }
    fclose(f);
    // 运行时字段不随配置恢复
    g_cfg.showMenu  = false;
    g_cfg.versionOk = true;
}

void config_save() {
    if (!g_cfgPath[0]) return;
    FILE* f = _wfopen(g_cfgPath, L"wb");
    if (!f) return;
    fwrite(&g_cfg, sizeof(g_cfg), 1, f);
    fclose(f);
}
