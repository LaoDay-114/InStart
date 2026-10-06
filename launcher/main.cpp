// ============================================================
// InStart 启动器：现代 UI（ImGui + Win32 + DX11）
// - 启动后扫描并列出检测到的 Minecraft 实例，用户选择后再注入
// - 注入通过子进程调用 InStartInjector.exe --inject <pid>
// - 注入器崩溃/失败时显示原因，界面不退出
// ============================================================
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "../injector/game_instances.h"

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")

// ---- D3D11 全局 ----
static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static IDXGISwapChain*         g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;

// ---- 应用状态 ----
static std::vector<GameInstance> g_instances;
static int      g_selected = -1;
static float    g_scanCooldown = 0.f;

static TitleKeywords g_kw;                 // 标题匹配关键词（内置 + 自定义）
static char          g_kwInput[128] = "";  // 新关键词输入框（UTF-8）

static std::atomic<bool> g_injecting{ false };
static std::string       g_log;            // 注入输出
static int               g_lastExit = -1;  // -1 无结果 0 成功 1 失败 2 崩溃
static bool              g_resultNew = false;
static int               g_injectedPid = 0;

// 重新扫描游戏实例
static void rescan() {
    g_instances.clear();
    g_selected = -1;
    GameInstance items[16];
    int n = scan_game_instances_kw(items, 16, &g_kw);
    g_instances.assign(items, items + n);
    if (n > 0) g_selected = 0;
    g_resultNew = false;
}

// 主题色
static const ImVec4 ACCENT   = ImVec4(0.45f, 0.40f, 0.95f, 1.0f);
static const ImVec4 ACCENT_H = ImVec4(0.55f, 0.50f, 1.00f, 1.0f);
static const ImVec4 OK_GREEN = ImVec4(0.30f, 0.85f, 0.45f, 1.0f);
static const ImVec4 ERR_RED  = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
static const ImVec4 BG_DARK  = ImVec4(0.075f, 0.08f, 0.10f, 1.0f);

// ============================================================
// D3D11 设备
// ============================================================
static bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags, fls, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx)))
        return false;
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return true;
}

static void cleanup_device() {
    if (g_rtv)  { g_rtv->Release();  g_rtv = nullptr; }
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx)  { g_ctx->Release();  g_ctx = nullptr; }
    if (g_dev)  { g_dev->Release();  g_dev = nullptr; }
}

static void reset_rtv() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

// ============================================================
// 注入子进程：重定向输出 + 等待退出 + 记录退出码
// ============================================================
static void inject_worker(DWORD pid) {
    g_log.clear();
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
        g_log = "[ERR] 无法创建输出管道\n";
        g_lastExit = 1; g_resultNew = true; g_injecting = false;
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
        g_log = buf;
        g_lastExit = 1; g_resultNew = true; g_injecting = false;
        return;
    }

    // 读输出直到管道关闭
    char chunk[512];
    DWORD got = 0;
    while (ReadFile(rPipe, chunk, sizeof chunk - 1, &got, nullptr) && got > 0) {
        chunk[got] = '\0';
        g_log += chunk;
    }
    CloseHandle(rPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    g_lastExit = (code == 0) ? 0 : (code == 2 ? 2 : 1);
    // 进程异常终止（非 0/1/2，如被杀软强杀）也按崩溃处理
    if (g_lastExit == 1 && code > 2) g_lastExit = 2;
    g_resultNew = true;
    g_injecting = false;
}

static void start_inject(DWORD pid) {
    if (g_injecting) return;
    g_injecting = true;
    g_injectedPid = (int)pid;
    std::thread(inject_worker, pid).detach();
}

// ============================================================
// 现代主题
// ============================================================
static void apply_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.f;
    s.ChildRounding = 8.f;
    s.FrameRounding = 6.f;
    s.PopupRounding = 8.f;
    s.ScrollbarRounding = 8.f;
    s.GrabRounding = 6.f;
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 8);
    s.WindowPadding = ImVec2(20, 16);
    s.ScrollbarSize = 10.f;
    s.WindowBorderSize = 0.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = BG_DARK;
    c[ImGuiCol_ChildBg]         = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_Text]            = ImVec4(0.92f, 0.93f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.50f, 0.52f, 0.58f, 1.0f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.14f, 0.15f, 0.19f, 1.0f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.18f, 0.19f, 0.24f, 1.0f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.22f, 0.23f, 0.29f, 1.0f);
    c[ImGuiCol_Button]          = ImVec4(0.20f, 0.21f, 0.26f, 1.0f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.28f, 0.29f, 0.36f, 1.0f);
    c[ImGuiCol_ButtonActive]    = ACCENT;
    c[ImGuiCol_Header]          = ImVec4(0.16f, 0.17f, 0.21f, 1.0f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.45f, 0.40f, 0.95f, 0.35f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.45f, 0.40f, 0.95f, 0.55f);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.30f, 0.31f, 0.38f, 1.0f);
    c[ImGuiCol_Separator]       = ImVec4(0.22f, 0.23f, 0.28f, 1.0f);
}

// ============================================================
// 主界面
// ============================================================
static void draw_ui() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- 头部 ----
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1
                    ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
    ImGui::TextColored(ACCENT_H, "InStart");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("Minecraft 辅助注入器");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- 实例列表区 ----
    float footerH = 120.f;
    ImGui::BeginChild("##list", ImVec2(0, -footerH), true);

    // 标题关键词管理（可折叠）
    if (ImGui::CollapsingHeader("标题匹配关键词")) {
        ImGui::Indent(8);
        ImGui::TextDisabled("窗口标题包含以下任一关键词即视为游戏实例");

        // 关键词标签流式排列；内置词不可删，自定义词可删
        int delIdx = -1;
        for (int i = 0; i < g_kw.count; ++i) {
            char u8[80];
            WideCharToMultiByte(CP_UTF8, 0, g_kw.items[i], -1, u8, sizeof u8, nullptr, nullptr);
            ImGui::PushID(100 + i);
            bool builtin = i < GI_BUILTIN_KEYWORDS;
            if (builtin) ImGui::BeginDisabled(true);
            ImGui::PushStyleColor(ImGuiCol_Button,
                builtin ? ImVec4(0.16f, 0.17f, 0.21f, 1.0f)
                        : ImVec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.35f));
            if (ImGui::SmallButton(builtin ? u8 : (std::string(u8) + "  ×").c_str()))
                if (!builtin) delIdx = i;
            ImGui::PopStyleColor();
            if (builtin) ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::SameLine(0, 8);
        }
        ImGui::NewLine();

        // 添加新关键词
        ImGui::SetNextItemWidth(260);
        bool add = ImGui::InputTextWithHint("##kw", "输入自定义标题关键词，回车添加",
                                            g_kwInput, sizeof g_kwInput,
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("添加")) add = true;
        if (add && g_kwInput[0] && g_kw.count < GI_MAX_KEYWORDS) {
            wchar_t w[64];
            if (MultiByteToWideChar(CP_UTF8, 0, g_kwInput, -1, w, 64) > 1) {
                wcscpy(g_kw.items[g_kw.count++], w);
                save_title_keywords(&g_kw);
                g_kwInput[0] = '\0';
                rescan();
            }
        }
        if (delIdx >= 0) { // 删除自定义关键词
            for (int i = delIdx; i < g_kw.count - 1; ++i)
                wcscpy(g_kw.items[i], g_kw.items[i + 1]);
            g_kw.count--;
            save_title_keywords(&g_kw);
            rescan();
        }
        ImGui::Unindent(8);
    }
    ImGui::Spacing();

    ImGui::TextDisabled("检测到的游戏实例（%d）", (int)g_instances.size());
    ImGui::Spacing();

    if (g_instances.empty()) {
        ImGui::Dummy(ImVec2(0, 30));
        float tw = ImGui::CalcTextSize("未检测到运行中的游戏").x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - tw) * 0.5f);
        ImGui::TextDisabled("未检测到运行中的游戏");
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() -
            ImGui::CalcTextSize("请启动 Minecraft 1.21 ~ 1.21.11（Fabric）后点击刷新").x) * 0.5f);
        ImGui::TextDisabled("请启动 Minecraft 1.21 ~ 1.21.11（Fabric）后点击刷新");
    } else {
        for (int i = 0; i < (int)g_instances.size(); ++i) {
            GameInstance& gi = g_instances[i];
            char label[320];
            snprintf(label, sizeof label, "%s", gi.title[0] ? gi.title : "(无标题)");
            char sub[96];
            snprintf(sub, sizeof sub, "PID %lu", gi.pid);

            ImGui::PushID(i);
            bool sel = (g_selected == i);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.30f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.40f));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 12));
            if (ImGui::Selectable("##inst", sel, 0, ImVec2(0, 52)))
                g_selected = i;
            ImGui::PopStyleVar();
            if (sel) ImGui::PopStyleColor(2);

            // 在 Selectable 上绘制两行文字
            ImVec2 p = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImFont* f = ImGui::GetFont();
            dl->AddText(f, 17.f, ImVec2(p.x + 14, p.y + 8),
                        ImGui::GetColorU32(ImGuiCol_Text), label);
            dl->AddText(f, 14.f, ImVec2(p.x + 14, p.y + 30),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
            // 选中指示条
            if (sel)
                dl->AddRectFilled(p, ImVec2(p.x + 4, p.y + 52),
                                  ImGui::GetColorU32(ACCENT), 2.f);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // ---- 底部操作区 ----
    ImGui::Spacing();

    // 结果横幅
    if (g_resultNew) {
        if (g_lastExit == 0)
            ImGui::TextColored(OK_GREEN, "注入完成！切回游戏按右 Alt 呼出菜单");
        else if (g_lastExit == 2)
            ImGui::TextColored(ERR_RED, "注入器异常终止，详见下方输出");
        else
            ImGui::TextColored(ERR_RED, "注入失败，详见下方输出");
    } else if (g_injecting) {
        ImGui::TextColored(ACCENT_H, "正在向 PID %d 注入...", g_injectedPid);
    } else {
        ImGui::TextDisabled("选择一个游戏实例后点击注入");
    }

    // 日志输出（有内容时显示）
    if (!g_log.empty() && g_resultNew && g_lastExit != 0) {
        ImGui::BeginChild("##log", ImVec2(0, 52), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, ERR_RED);
        ImGui::TextUnformatted(g_log.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
    }

    // 按钮行
    float btnH = 40.f;
    float injectW = 160.f, refreshW = 100.f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x
                         - injectW - 10 - refreshW);

    ImGui::BeginDisabled(g_injecting);
    if (ImGui::Button("刷新", ImVec2(refreshW, btnH)))
        rescan();
    ImGui::SameLine();
    bool wasInjecting = g_injecting; // 点击后 start_inject 会改变 g_injecting，先保存
    bool canInject = g_selected >= 0 && g_selected < (int)g_instances.size() && !wasInjecting;
    if (!wasInjecting) {
        ImGui::PushStyleColor(ImGuiCol_Button, ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ACCENT_H);
    }
    ImGui::BeginDisabled(!canInject);
    if (ImGui::Button(wasInjecting ? "注入中..." : "注  入", ImVec2(injectW, btnH)))
        start_inject(g_instances[g_selected].pid);
    ImGui::EndDisabled();
    if (!wasInjecting) ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::End();
}

// ============================================================
// Win32 骨架
// ============================================================
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_dev && w != SIZE_MINIMIZED) { g_resizeW = LOWORD(l); g_resizeH = HIWORD(l); }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    WNDCLASSEXW wc = { sizeof wc, CS_CLASSDC, wnd_proc, 0, 0, inst,
                       nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr,
                       L"InStartLauncher", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"InStart",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 560,
        nullptr, nullptr, inst, nullptr);
    if (!create_device(hwnd)) { cleanup_device(); return 1; }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // 不生成 imgui.ini

    apply_style();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    // 中文字体：微软雅黑（ttc 取第一个字体），失败退回内置字体
    ImFontConfig fc;
    fc.FontNo = 0;
    ImFont* cn = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 18.f, &fc,
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    // 标题用大一号
    ImFontConfig fc2;
    fc2.FontNo = 0;
    ImFont* title = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 26.f, &fc2,
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    if (!cn) io.Fonts->AddFontDefault();
    (void)title;

    // 初始加载关键词 + 扫描
    load_title_keywords(&g_kw);
    rescan();

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        if (g_resizeW && g_resizeH) {
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            reset_rtv();
            g_resizeW = g_resizeH = 0;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        draw_ui();
        ImGui::Render();

        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        float clear[4] = { BG_DARK.x, BG_DARK.y, BG_DARK.z, 1.f };
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    return 0;
}
