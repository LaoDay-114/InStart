// ============================================================
// InStart 配置持久化：
//   首次启动在 DLL 同目录创建 InStartConfig 文件夹，
//   配置保存到 InStartConfig\config.txt（二进制整个结构体）
// ============================================================
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include "config.h"

static wchar_t g_cfgPath[MAX_PATH] = {};

// 配置文件头：识别有效配置，结构体布局变化（字段增删/改序）后自动弃用旧文件。
// 不能只比较文件大小——字段插入+尾部删除可能使总大小恰好不变，导致读入错位数据
// （典型症状：bind[0] 读到旧 bind[1]，菜单快捷键失效）。
// 修改 InStartConfig 布局时必须递增 CFG_VER。
static const uint32_t CFG_MAGIC = 0x54534E49; // "INST" 小端
static const uint32_t CFG_VER   = 2;          // 当前布局版本
struct CfgHeader { uint32_t magic; uint32_t ver; uint32_t size; };

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
    CfgHeader h = {};
    if (fread(&h, sizeof h, 1, f) != 1 ||
        h.magic != CFG_MAGIC || h.ver != CFG_VER || h.size != sizeof(g_cfg)) {
        fclose(f);   // 旧版/不兼容/损坏配置 → 保持默认值
        return;
    }
    if (fread(&g_cfg, sizeof(g_cfg), 1, f) != 1) {
        fclose(f);
        return;
    }
    fclose(f);
    // 运行时字段不随配置恢复
    g_cfg.showMenu  = false;
}

void config_save() {
    if (!g_cfgPath[0]) return;
    FILE* f = _wfopen(g_cfgPath, L"wb");
    if (!f) return;
    CfgHeader h = { CFG_MAGIC, CFG_VER, (uint32_t)sizeof(g_cfg) };
    fwrite(&h, sizeof h, 1, f);
    fwrite(&g_cfg, sizeof(g_cfg), 1, f);
    fclose(f);
}
