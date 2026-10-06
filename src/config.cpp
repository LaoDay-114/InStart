// ============================================================
// InStart 配置持久化：
//   首次启动在 DLL 同目录创建 InStartConfig 文件夹，
//   配置保存到 InStartConfig\config.txt（二进制整个结构体）
// ============================================================
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "config.h"

static wchar_t g_cfgPath[MAX_PATH] = {};

// 配置文件头：识别有效配置，结构体布局变化（字段增删/改序）后自动弃用旧文件。
// 不能只比较文件大小——字段插入+尾部删除可能使总大小恰好不变，导致读入错位数据
// （典型症状：bind[0] 读到旧 bind[1]，菜单快捷键失效）。
// 修改 InStartConfig 布局时必须递增 CFG_VER。
static const uint32_t CFG_MAGIC = 0x54534E49; // "INST" 小端
static const uint32_t CFG_VER   = 3;          // 当前布局版本
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
    config_sanitize(); // 校验/自动修正无效项（发生修正会回写文件）
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

// ---- 配置归一化 / 自动修正 ----
// 判断 vk 是否为可绑定按键：0=未绑定合法；鼠标键、Windows 保留/未分配段非法
static bool is_bindable_vk(int vk) {
    if (vk == 0) return true;
    if (vk < 1 || vk > 254) return false;
    if (vk >= VK_LBUTTON && vk <= VK_XBUTTON2) return false; // 鼠标 0x01..0x06
    // Windows 保留/未分配虚拟键码：绑定到这些"不存在的键"无意义
    if ((vk >= 0x0B && vk <= 0x0F) ||
        (vk >= 0x3A && vk <= 0x40) ||
        (vk >= 0x88 && vk <= 0x8F) ||
        (vk >= 0x97 && vk <= 0x9F)) return false;
    return true;
}

static bool in_range(float v, float lo, float hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}

// 加载后校验全部配置项：发现指向不存在模块/无效按键/越界或损坏值时自动修正，
// 并回写文件使其重新自洽，避免坏配置把功能锁死（如菜单键丢失无法呼出）
void config_sanitize() {
    bool fixed = false;

    // 布尔字段：非规范字节（非 0/1 的垃圾位）归一为规范 true/false
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

    // 数值：NaN/越界 → 恢复默认
    if (!in_range(g_cfg.flySpeed, 0.5f, 8.0f))  { g_cfg.flySpeed  = 2.0f; fixed = true; }
    if (!in_range(g_cfg.speedMult, 1.1f, 5.0f)) { g_cfg.speedMult = 2.0f; fixed = true; }
    if (!in_range(g_cfg.auraRange, 1.5f, 5.0f)) { g_cfg.auraRange = 3.0f; fixed = true; }
    if (g_cfg.noFallMode < 0 || g_cfg.noFallMode > 1) { g_cfg.noFallMode = 0; fixed = true; }

    // 快捷键：无效 VK（指向不存在的键/模块）→ 未绑定
    for (int i = 0; i < BIND_COUNT; ++i) {
        if (!is_bindable_vk(g_cfg.bind[i])) { g_cfg.bind[i] = 0; fixed = true; }
    }
    // 菜单键是唯一呼出入口：一旦丢失将无法再打开菜单（入口模块缺失），
    // 自动恢复为默认右键 Alt
    if (g_cfg.bind[BIND_MENU] == 0) {
        g_cfg.bind[BIND_MENU] = VK_RMENU;
        fixed = true;
    }

    if (fixed) config_save();
}
