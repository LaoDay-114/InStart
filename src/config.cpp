#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "config.h"

static wchar_t g_cfgPath[MAX_PATH] = {};

static const uint32_t CFG_MAGIC = 0x54534E49;
static const uint32_t CFG_VER   = 4;
struct CfgHeader { uint32_t magic; uint32_t ver; uint32_t size; };

void config_init_path(void* moduleHandle) {
    wchar_t dir[MAX_PATH] = {};
    if (!GetModuleFileNameW((HMODULE)moduleHandle, dir, MAX_PATH)) return;
    wchar_t* p = wcsrchr(dir, L'\\');
    if (!p) return;
    *p = L'\0';
    wcscat_s(dir, L"\\InStartConfig");
    CreateDirectoryW(dir, nullptr);
    _snwprintf_s(g_cfgPath, MAX_PATH, L"%s\\config.txt", dir);
}

void config_load() {
    if (!g_cfgPath[0]) return;
    FILE* f = _wfopen(g_cfgPath, L"rb");
    if (!f) return;
    CfgHeader h = {};
    if (fread(&h, sizeof h, 1, f) != 1 ||
        h.magic != CFG_MAGIC || h.ver != CFG_VER || h.size != sizeof(g_cfg)) {
        fclose(f);
        return;
    }
    if (fread(&g_cfg, sizeof(g_cfg), 1, f) != 1) {
        fclose(f);
        return;
    }
    fclose(f);
    config_sanitize();
    g_cfg.showMenu = false;
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

static bool is_bindable_vk(int vk) {
    if (vk == 0) return true;
    if (vk < 1 || vk > 254) return false;
    if (vk >= VK_LBUTTON && vk <= VK_XBUTTON2) return false;
    if ((vk >= 0x0B && vk <= 0x0F) ||
        (vk >= 0x3A && vk <= 0x40) ||
        (vk >= 0x88 && vk <= 0x8F) ||
        (vk >= 0x97 && vk <= 0x9F)) return false;
    return true;
}

static bool in_range(float v, float lo, float hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}

void config_sanitize() {
    bool fixed = false;

    bool* bools[] = {
        &g_cfg.hud, &g_cfg.fly, &g_cfg.speed, &g_cfg.fullbright,
        &g_cfg.esp, &g_cfg.espMobsOnly, &g_cfg.noFall,
        &g_cfg.killaura, &g_cfg.kaExcludePlayers, &g_cfg.kaExcludeMobs,
        &g_cfg.autoTotem,
    };
    for (bool* b : bools) {
        unsigned char raw = *(unsigned char*)b;
        if (raw != 0 && raw != 1) { *b = true; fixed = true; }
    }

    if (!in_range(g_cfg.flySpeed, 0.5f, 8.0f))  { g_cfg.flySpeed  = 2.0f; fixed = true; }
    if (!in_range(g_cfg.speedMult, 1.1f, 5.0f)) { g_cfg.speedMult = 2.0f; fixed = true; }
    if (!in_range(g_cfg.auraRange, 1.5f, 5.0f)) { g_cfg.auraRange = 3.0f; fixed = true; }

    for (int i = 0; i < BIND_COUNT; ++i) {
        if (!is_bindable_vk(g_cfg.bind[i])) { g_cfg.bind[i] = 0; fixed = true; }
    }
    if (g_cfg.bind[BIND_MENU] == 0) {
        g_cfg.bind[BIND_MENU] = VK_RMENU;
        fixed = true;
    }

    if (fixed) config_save();
}
