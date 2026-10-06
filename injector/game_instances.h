// ============================================================
// InStart 游戏实例检测（注入器 CLI 与启动器 UI 共用）
// 匹配条件：类名包含 GLFW 且标题包含 Minecraft/Lunar/Badlion/Feather
// ============================================================
#pragma once
#include <windows.h>

struct GameInstance {
    DWORD pid;
    HWND  hwnd;
    char  title[256];
    char  cls[64];
};

struct EnumCtx {
    GameInstance* items;
    int           count;
    int           capacity;
};

static BOOL CALLBACK enum_game_window(HWND h, LPARAM lp) {
    EnumCtx* ctx = (EnumCtx*)lp;
    if (ctx->count >= ctx->capacity) return FALSE;
    if (!IsWindowVisible(h)) return TRUE;

    char cls[64] = {}, title[256] = {};
    GetClassNameA(h, cls, sizeof cls);
    GetWindowTextA(h, title, sizeof title);

    bool isGLFW = strstr(cls, "GLFW") != nullptr;
    bool titleMatch = strstr(title, "Minecraft") != nullptr
                   || strstr(title, "Lunar") != nullptr
                   || strstr(title, "Badlion") != nullptr
                   || strstr(title, "Feather") != nullptr;
    if (!isGLFW || !titleMatch) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    // 同一进程多个匹配窗口时只保留第一个（避免重复实例）
    for (int i = 0; i < ctx->count; ++i)
        if (ctx->items[i].pid == pid) return TRUE;

    GameInstance& gi = ctx->items[ctx->count++];
    gi.pid = pid;
    gi.hwnd = h;
    lstrcpynA(gi.title, title, sizeof gi.title);
    lstrcpynA(gi.cls, cls, sizeof gi.cls);
    return TRUE;
}

// 枚举当前运行的游戏实例；返回数量（<= capacity）
inline int scan_game_instances(GameInstance* out, int capacity) {
    EnumCtx ctx{ out, 0, capacity };
    EnumWindows(enum_game_window, (LPARAM)&ctx);
    return ctx.count;
}
