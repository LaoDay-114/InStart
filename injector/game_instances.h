// ============================================================
// InStart 游戏实例检测（注入器 CLI 与启动器 UI 共用）
// 匹配条件：类名包含 GLFW 且标题命中关键词
// 关键词 = 内置默认（Minecraft/Lunar/Badlion/Feather）
//        + exe 同目录 title_keywords.txt 中每行一个的自定义词（UTF-8）
// ============================================================
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>

struct GameInstance {
    DWORD pid;
    HWND  hwnd;
    char  title[256]; // UTF-8（ImGui 直接渲染）
    char  cls[64];
};

#define GI_MAX_KEYWORDS 16
#define GI_BUILTIN_KEYWORDS 4

struct TitleKeywords {
    wchar_t items[GI_MAX_KEYWORDS][64];
    int     count;
};

// exe 同目录 title_keywords.txt 完整路径
inline void keywords_file_path(wchar_t* out, size_t max) {
    GetModuleFileNameW(nullptr, out, (DWORD)max);
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat(out, L"title_keywords.txt");
}

// 加载关键词：内置默认 + 文件追加（文件不存在或为空时仅默认）
inline void load_title_keywords(TitleKeywords* kw) {
    kw->count = 0;
    wcscpy(kw->items[kw->count++], L"Minecraft");
    wcscpy(kw->items[kw->count++], L"Lunar");
    wcscpy(kw->items[kw->count++], L"Badlion");
    wcscpy(kw->items[kw->count++], L"Feather");

    wchar_t path[MAX_PATH];
    keywords_file_path(path, MAX_PATH);
    FILE* f = _wfopen(path, L"rb");
    if (!f) return;
    char line[256];
    while (kw->count < GI_MAX_KEYWORDS && fgets(line, sizeof line, f)) {
        char* s = line;
        // 去 UTF-8 BOM
        if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
            (unsigned char)s[2] == 0xBF) s += 3;
        size_t len = strlen(s);
        while (len && (s[len-1] == '\n' || s[len-1] == '\r' ||
                       s[len-1] == ' '  || s[len-1] == '\t')) s[--len] = '\0';
        if (!len || s[0] == '#') continue;
        if (MultiByteToWideChar(CP_UTF8, 0, s, -1,
                                kw->items[kw->count], 64) > 0)
            kw->count++;
    }
    fclose(f);
}

// 保存自定义关键词（内置默认不写入文件）；返回是否成功
inline bool save_title_keywords(const TitleKeywords* kw) {
    wchar_t path[MAX_PATH];
    keywords_file_path(path, MAX_PATH);
    FILE* f = _wfopen(path, L"wb");
    if (!f) return false;
    for (int i = GI_BUILTIN_KEYWORDS; i < kw->count; ++i) {
        char u8[192];
        int n = WideCharToMultiByte(CP_UTF8, 0, kw->items[i], -1, u8, sizeof u8, nullptr, nullptr);
        if (n > 0) fputs(u8, f); // 含结尾 \0，换成 \n
        // 重写：上面 fputs 会带 \0，改用逐字符
    }
    fclose(f);
    // 上面的写法会混入 \0，重新正确写一遍
    f = _wfopen(path, L"wb");
    if (!f) return false;
    for (int i = GI_BUILTIN_KEYWORDS; i < kw->count; ++i) {
        char u8[192];
        int n = WideCharToMultiByte(CP_UTF8, 0, kw->items[i], -1, u8, sizeof u8, nullptr, nullptr);
        if (n > 1) { fwrite(u8, 1, n - 1, f); fputc('\n', f); }
    }
    fclose(f);
    return true;
}

struct EnumCtx {
    GameInstance* items;
    int           count;
    int           capacity;
    const TitleKeywords* kw;
};

static BOOL CALLBACK enum_game_window(HWND h, LPARAM lp) {
    EnumCtx* ctx = (EnumCtx*)lp;
    if (ctx->count >= ctx->capacity) return FALSE;
    if (!IsWindowVisible(h)) return TRUE;

    char cls[64] = {};
    wchar_t wtitle[256] = {};
    GetClassNameA(h, cls, sizeof cls);
    GetWindowTextW(h, wtitle, 256);
    if (!wtitle[0]) return TRUE;

    if (!strstr(cls, "GLFW")) return TRUE;
    bool titleMatch = false;
    for (int i = 0; i < ctx->kw->count; ++i) {
        if (wcsstr(wtitle, ctx->kw->items[i])) { titleMatch = true; break; }
    }
    if (!titleMatch) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    // 同一进程多个匹配窗口时只保留第一个（避免重复实例）
    for (int i = 0; i < ctx->count; ++i)
        if (ctx->items[i].pid == pid) return TRUE;

    GameInstance& gi = ctx->items[ctx->count++];
    gi.pid = pid;
    gi.hwnd = h;
    WideCharToMultiByte(CP_UTF8, 0, wtitle, -1, gi.title, sizeof gi.title, nullptr, nullptr);
    lstrcpynA(gi.cls, cls, sizeof gi.cls);
    return TRUE;
}

// 枚举当前运行的游戏实例；返回数量（<= capacity）
inline int scan_game_instances_kw(GameInstance* out, int capacity, const TitleKeywords* kw) {
    EnumCtx ctx{ out, 0, capacity, kw };
    EnumWindows(enum_game_window, (LPARAM)&ctx);
    return ctx.count;
}

inline int scan_game_instances(GameInstance* out, int capacity) {
    TitleKeywords kw;
    load_title_keywords(&kw);
    return scan_game_instances_kw(out, capacity, &kw);
}
