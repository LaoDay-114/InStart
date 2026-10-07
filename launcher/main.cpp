#include "eui_neo.h"

#include <windows.h>
#include <dwmapi.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "game_instances.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace app {
namespace {

using eui::Color;

const Color kAccent    = components::theme::defaultPrimary();
const Color kAccentHi  = components::theme::color(0.35f, 0.55f, 0.95f);
const Color kOkGreen   = components::theme::color(0.30f, 0.85f, 0.45f);
const Color kErrRed    = components::theme::color(0.95f, 0.35f, 0.35f);
const Color kText      = components::theme::color(1.00f, 1.00f, 1.00f);
const Color kMuted     = components::theme::withOpacity(components::theme::dark().text, 0.55f);
const Color kCard      = components::theme::dark().surface;
const Color kCardHover = components::theme::dark().surfaceHover;
const Color kBorder    = components::theme::withOpacity(components::theme::dark().border, 0.60f);

std::vector<GameInstance> g_instances;
int         g_selected = -1;

TitleKeywords g_kw;
std::string   g_kwInput;

std::atomic<bool> g_injecting{ false };
std::mutex        g_logMtx;
std::string       g_log;
std::atomic<int>  g_lastExit{ -1 };
std::atomic<bool> g_resultNew{ false };
int               g_injectedPid = 0;

void rescan() {
    g_instances.clear();
    g_selected = -1;
    GameInstance items[16];
    int n = scan_game_instances_kw(items, 16, &g_kw);
    g_instances.assign(items, items + n);
    if (n > 0) g_selected = 0;
    g_resultNew = false;
}

void set_result(const std::string& log, int exit) {
    { std::lock_guard<std::mutex> lk(g_logMtx); g_log = log; }
    g_lastExit = exit;
    g_resultNew = true;
    g_injecting = false;
    requestUpdate();
}

void inject_worker(DWORD pid) {
    { std::lock_guard<std::mutex> lk(g_logMtx); g_log.clear(); }
    g_lastExit = -1;
    g_resultNew = false;

    wchar_t selfDir[MAX_PATH];
    GetModuleFileNameW(nullptr, selfDir, MAX_PATH);
    wchar_t* slash = wcsrchr(selfDir, L'\\');
    if (slash) *(slash + 1) = L'\0';
    std::wstring injector = std::wstring(selfDir) + L"InStartInjector.exe";

    SECURITY_ATTRIBUTES sa{ sizeof sa, nullptr, TRUE };
    HANDLE rPipe = nullptr, wPipe = nullptr;
    if (!CreatePipe(&rPipe, &wPipe, &sa, 0)) {
        set_result("[ERR] 无法创建输出管道\n", 1);
        return;
    }
    SetHandleInformation(rPipe, HANDLE_FLAG_INHERIT, 0);

    wchar_t cmdline[MAX_PATH * 2];
    swprintf(cmdline, L"\"%s\" --inject %lu", injector.c_str(), pid);

    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wPipe;
    si.hStdError = wPipe;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, cmdline, nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, selfDir, &si, &pi);
    CloseHandle(wPipe);

    if (!ok) {
        CloseHandle(rPipe);
        char buf[256];
        snprintf(buf, sizeof buf,
                 "[ERR] 无法启动注入器（err=%lu）\n请确认 InStartInjector.exe 与本程序在同一目录\n",
                 GetLastError());
        set_result(buf, 1);
        return;
    }

    char chunk[512];
    DWORD got = 0;
    while (ReadFile(rPipe, chunk, sizeof chunk - 1, &got, nullptr) && got > 0) {
        chunk[got] = '\0';
        { std::lock_guard<std::mutex> lk(g_logMtx); g_log += chunk; }
        requestUpdate();
    }
    CloseHandle(rPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    int exit = (code == 0) ? 0 : (code == 2 ? 2 : 1);
    if (exit == 1 && code > 2) exit = 2;
    g_lastExit = exit;
    g_resultNew = true;
    g_injecting = false;
    requestUpdate();
}

void start_inject(DWORD pid) {
    if (g_injecting) return;
    g_injecting = true;
    g_injectedPid = (int)pid;
    std::thread(inject_worker, pid).detach();
}

float text_width(const std::string& s, float fontSize) {
    float w = 0.0f;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { w += fontSize * 0.52f; ++i; }
        else {
            int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
            w += fontSize;
            i += (size_t)n;
        }
    }
    return w;
}

void add_keyword() {
    if (g_kwInput.empty() || g_kw.count >= GI_MAX_KEYWORDS) return;
    wchar_t w[64];
    if (MultiByteToWideChar(CP_UTF8, 0, g_kwInput.c_str(), -1, w, 64) > 1) {
        wcscpy(g_kw.items[g_kw.count++], w);
        save_title_keywords(&g_kw);
        g_kwInput.clear();
        rescan();
    }
}

void remove_keyword(int index) {
    if (index < GI_BUILTIN_KEYWORDS || index >= g_kw.count) return;
    for (int i = index; i < g_kw.count - 1; ++i)
        wcscpy(g_kw.items[i], g_kw.items[i + 1]);
    g_kw.count--;
    save_title_keywords(&g_kw);
    rescan();
}

std::string keyword_u8(int i) {
    char u8[80];
    WideCharToMultiByte(CP_UTF8, 0, g_kw.items[i], -1, u8, sizeof u8, nullptr, nullptr);
    return u8;
}

void dark_title_bar_once() {
    static bool done = false;
    if (done) return;
    done = true;
    HWND hwnd = FindWindowW(nullptr, L"InStart");
    if (!hwnd) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    COLORREF caption = RGB(0x1A, 0x1A, 0x1F);
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof caption);
}

void build_keyword_section(eui::Ui& ui, float width) {
    components::card(ui, "card.kw")
        .width(width)
        .radius(12.0f)
        .padding(16.0f)
        .content([&] {
            ui.column("kw.col").width(width - 32.0f).gap(10.0f).content([&] {
                ui.text("kw.title")
                    .text("标题匹配关键词")
                    .fontSize(15.0f)
                    .color(kText)
                    .build();
                ui.text("kw.hint")
                    .text("窗口标题包含以下任一关键词即视为游戏实例")
                    .fontSize(12.0f)
                    .color(kMuted)
                    .build();

                ui.flow("kw.chips").width(width - 32.0f).gap(8.0f).content([&] {
                    for (int i = 0; i < g_kw.count; ++i) {
                        const std::string label = keyword_u8(i);
                        const bool builtin = i < GI_BUILTIN_KEYWORDS;
                        const float w = text_width(label, 13.0f) + (builtin ? 24.0f : 40.0f);
                        components::button(ui, "kw.chip." + std::to_string(i))
                            .size(w, 26.0f)
                            .fontSize(13.0f)
                            .radius(13.0f)
                            .text(builtin ? label : label + "  ×")
                            .theme(components::theme::dark(), false)
                            .disabled(builtin)
                            .onClick([i] { remove_keyword(i); })
                            .build();
                    }
                }).build();

                ui.row("kw.add").width(width - 32.0f).gap(8.0f).content([&] {
                    components::input(ui, "kw.input")
                        .size(width - 32.0f - 84.0f, 32.0f)
                        .fontSize(13.0f)
                        .value(g_kwInput)
                        .placeholder("输入自定义标题关键词，回车添加")
                        .onChange([](const std::string& v) { g_kwInput = v; })
                        .onEnter([] { add_keyword(); })
                        .build();
                    components::button(ui, "kw.add.btn")
                        .size(76.0f, 32.0f)
                        .fontSize(13.0f)
                        .radius(8.0f)
                        .text("添加")
                        .theme(components::theme::dark(), false)
                        .onClick([] { add_keyword(); })
                        .build();
                }).build();
            }).build();
        })
        .build();
}

void build_instance_row(eui::Ui& ui, int i, const GameInstance& gi, float width) {
    const bool sel = g_selected == i;
    const char* title = gi.title[0] ? gi.title : "(无标题)";
    char sub[96];
    snprintf(sub, sizeof sub, "PID %lu", gi.pid);

    const Color normal  = sel ? components::theme::withAlpha(kAccent, 0.20f) : kCard;
    const Color hovered = sel ? components::theme::withAlpha(kAccent, 0.30f) : kCardHover;
    const Color pressed = sel ? components::theme::withAlpha(kAccent, 0.40f)
                              : components::theme::dark().surfaceActive;

    ui.stack("inst." + std::to_string(i))
        .width(width)
        .height(54.0f)
        .content([&] {
            ui.rect("inst.bg")
                .size(width, 54.0f)
                .states(normal, hovered, pressed)
                .radius(10.0f)
                .border(1.0f, sel ? components::theme::withAlpha(kAccent, 0.65f)
                                  : Color{0, 0, 0, 0})
                .onClick([i] { g_selected = i; })
                .build();
            if (sel) {
                ui.rect("inst.bar")
                    .position(0.0f, 9.0f)
                    .size(4.0f, 36.0f)
                    .color(kAccent)
                    .radius(2.0f)
                    .build();
            }
            ui.column("inst.text")
                .position(16.0f, 9.0f)
                .width(width - 32.0f)
                .gap(3.0f)
                .content([&] {
                    ui.text("inst.title")
                        .size(width - 32.0f, 20.0f)
                        .text(title)
                        .fontSize(15.0f)
                        .color(kText)
                        .verticalAlign(eui::VerticalAlign::Center)
                        .build();
                    ui.text("inst.pid")
                        .size(width - 32.0f, 16.0f)
                        .text(sub)
                        .fontSize(12.0f)
                        .color(kMuted)
                        .verticalAlign(eui::VerticalAlign::Center)
                        .build();
                })
                .build();
        })
        .build();
}

void build_instance_section(eui::Ui& ui, float width, float height) {
    components::card(ui, "card.inst")
        .width(width)
        .height(height)
        .radius(12.0f)
        .padding(16.0f)
        .content([&] {
            const float innerW = width - 32.0f;
            const float innerH = height - 32.0f;
            ui.column("inst.col").size(innerW, innerH).gap(10.0f).content([&] {
                ui.text("inst.count")
                    .size(innerW, 18.0f)
                    .text("检测到的游戏实例（" + std::to_string(g_instances.size()) + "）")
                    .fontSize(13.0f)
                    .color(kMuted)
                    .build();

                const float listH = innerH - 28.0f;
                if (g_instances.empty()) {
                    ui.stack("inst.empty")
                        .size(innerW, listH)
                        .align(eui::Align::CENTER, eui::Align::CENTER)
                        .content([&] {
                            ui.column("inst.empty.col")
                                .gap(8.0f)
                                .alignItems(eui::Align::CENTER)
                                .content([&] {
                                    ui.text("inst.empty.1")
                                        .text("未检测到运行中的游戏")
                                        .fontSize(15.0f)
                                        .color(kMuted)
                                        .build();
                                    ui.text("inst.empty.2")
                                        .text("请启动 Minecraft 1.21 ~ 1.21.11（Fabric）后点击刷新")
                                        .fontSize(12.0f)
                                        .color(kMuted)
                                        .build();
                                })
                                .build();
                        })
                        .build();
                } else {
                    components::scrollView(ui, "inst.list")
                        .size(innerW, listH)
                        .gap(8.0f)
                        .content([&](eui::Ui& sui, float contentW, float) {
                            for (int i = 0; i < (int)g_instances.size(); ++i)
                                build_instance_row(sui, i, g_instances[i], contentW);
                        })
                        .build();
                }
            }).build();
        })
        .build();
}

void build_status(eui::Ui& ui, float width) {
    std::string msg;
    Color color = kMuted;
    if (g_resultNew) {
        if (g_lastExit == 0)      { msg = "注入完成！切回游戏按右 Alt 呼出菜单"; color = kOkGreen; }
        else if (g_lastExit == 2) { msg = "注入器异常终止，详见下方输出";        color = kErrRed; }
        else                      { msg = "注入失败，详见下方输出";              color = kErrRed; }
    } else if (g_injecting) {
        msg = "正在向 PID " + std::to_string(g_injectedPid) + " 注入...";
        color = kAccentHi;
    } else {
        msg = "选择一个游戏实例后点击注入";
    }
    ui.text("status")
        .size(width, 20.0f)
        .text(msg)
        .fontSize(13.0f)
        .color(color)
        .verticalAlign(eui::VerticalAlign::Center)
        .build();
}

void build_log(eui::Ui& ui, float width) {
    std::string log;
    { std::lock_guard<std::mutex> lk(g_logMtx); log = g_log; }
    if (!g_resultNew || g_lastExit == 0 || log.empty()) return;

    components::card(ui, "card.log")
        .width(width)
        .height(110.0f)
        .radius(12.0f)
        .padding(12.0f)
        .content([&] {
            components::scrollView(ui, "log.scroll")
                .size(width - 24.0f, 86.0f)
                .content([&](eui::Ui& sui, float contentW, float) {
                    sui.text("log.text")
                        .width(contentW)
                        .text(log)
                        .fontSize(12.0f)
                        .lineHeight(17.0f)
                        .color(kErrRed)
                        .wrap(true)
                        .build();
                })
                .build();
        })
        .build();
}

void build_footer(eui::Ui& ui, float width) {
    const bool injecting = g_injecting;
    const bool canInject = !injecting && g_selected >= 0 &&
                           g_selected < (int)g_instances.size();

    ui.row("footer")
        .size(width, 44.0f)
        .gap(10.0f)
        .justifyContent(eui::Align::END)
        .alignItems(eui::Align::CENTER)
        .content([&] {
            components::button(ui, "btn.refresh")
                .size(100.0f, 40.0f)
                .text("刷新")
                .fontSize(15.0f)
                .theme(components::theme::dark(), false)
                .disabled(injecting)
                .onClick([] { rescan(); })
                .build();
            components::button(ui, "btn.inject")
                .size(160.0f, 40.0f)
                .text(injecting ? "注入中..." : "注  入")
                .fontSize(15.0f)
                .theme(components::theme::dark(), true)
                .disabled(!canInject)
                .onClick([] {
                    if (g_selected >= 0 && g_selected < (int)g_instances.size())
                        start_inject(g_instances[g_selected].pid);
                })
                .build();
        })
        .build();
}

}

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("InStart")
        .pageId("launcher")
        .clearColor(components::theme::dark().background)
        .windowSize(560, 660)
        .resizable(false)
        .textFont("C:/Windows/Fonts/msyh.ttc")
        .fps(90.0);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    dark_title_bar_once();
    static bool first = true;
    if (first) {
        first = false;
        load_title_keywords(&g_kw);
        rescan();
    }

    const float pad = 24.0f;
    const float w = screen.width - pad * 2.0f;
    const float headerH = 36.0f;
    const float footerH = 44.0f;
    const float statusH = 20.0f;
    const float gap = 12.0f;
    const bool showLog = g_resultNew && g_lastExit > 0;
    const float logH = showLog ? 110.0f + gap : 0.0f;
    const float kwH = 32.0f + 16.0f + 10.0f + 18.0f + 8.0f + 26.0f + 10.0f + 32.0f + 10.0f;
    const float instH = screen.height - pad * 2.0f - headerH - kwH - statusH -
                        footerH - logH - gap * 4.0f;

    ui.column("root")
        .size(screen.width, screen.height)
        .padding(pad)
        .gap(gap)
        .content([&] {
            ui.row("header").size(w, headerH).gap(12.0f)
                .alignItems(eui::Align::CENTER)
                .content([&] {
                    ui.text("header.title")
                        .text("InStart")
                        .fontSize(26.0f)
                        .color(kAccentHi)
                        .build();
                    ui.text("header.sub")
                        .text("Minecraft 辅助注入器")
                        .fontSize(13.0f)
                        .color(kMuted)
                        .build();
                })
                .build();

            build_keyword_section(ui, w);
            build_instance_section(ui, w, instH);
            build_status(ui, w);
            build_log(ui, w);
            build_footer(ui, w);
        })
        .build();
}

}
