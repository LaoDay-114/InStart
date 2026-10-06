// 更新器：查询 GitHub Release，下载并替换有变化的文件。
// 无参数启动为图形界面；--silent 静默检查并自动安装（无界面）。
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <shellapi.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "update_check.h"

#ifndef INST_VERSION
#define INST_VERSION "dev"
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ---------- 文件操作 ----------

static std::wstring exe_dir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    return p.substr(0, s + 1);
}

static bool valid_name(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    if (n.find("..") != std::string::npos) return false;
    for (char c : n)
        if (c == '/' || c == '\\') return false;
    return true;
}

static bool read_state(const std::wstring& dir, std::string& tag) {
    FILE* f = _wfopen((dir + L"InStartUpdate.state").c_str(), L"rb");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "installed=", 10)) {
            tag = line + 10;
            while (!tag.empty() && (tag.back() == '\n' || tag.back() == '\r'))
                tag.pop_back();
        }
    }
    fclose(f);
    return !tag.empty();
}

static void write_state(const std::wstring& dir, const std::string& tag) {
    FILE* f = _wfopen((dir + L"InStartUpdate.state").c_str(), L"wb");
    if (!f) return;
    fprintf(f, "installed=%s\n", tag.c_str());
    fclose(f);
}

struct Placed {
    std::wstring final, old;
};

// 下载并替换单个文件：先下到 .new，原文件改名 .old，成功后清理
static int install_asset(const UpdateAsset& a, const std::wstring& dir,
                         std::vector<Placed>& placed,
                         DownloadProgress cb = nullptr, void* user = nullptr) {
    if (!valid_name(a.name)) return 1;

    std::wstring wname = utf8_to_wide(a.name);
    std::wstring finalPath = dir + wname;
    std::wstring tmpPath = finalPath + L".new";
    std::wstring oldPath = finalPath + L".old";

    DeleteFileW(oldPath.c_str());

    if (!download_file(a.url, tmpPath, cb, user)) {
        DeleteFileW(tmpPath.c_str());
        return 1;
    }

    bool moved = MoveFileExW(finalPath.c_str(), oldPath.c_str(),
                             MOVEFILE_REPLACE_EXISTING);
    if (!moved && GetLastError() != ERROR_FILE_NOT_FOUND) {
        DeleteFileW(tmpPath.c_str());
        return 1;
    }

    if (!MoveFileExW(tmpPath.c_str(), finalPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        if (moved)
            MoveFileExW(oldPath.c_str(), finalPath.c_str(),
                        MOVEFILE_REPLACE_EXISTING);
        return 1;
    }

    // 运行中的 exe/dll 删不掉，残留的 .old 下次更新时再清理
    DeleteFileW(oldPath.c_str());
    placed.push_back({ finalPath, moved ? oldPath : L"" });
    return 0;
}

static int install(const UpdateInfo& info, const std::wstring& dir,
                   std::vector<Placed>& placed) {
    for (const UpdateAsset& a : info.assets)
        if (install_asset(a, dir, placed) != 0) return 1;
    return placed.empty() && !info.assets.empty() ? 1 : 0;
}

// ---------- 静默模式 ----------

static int run_silent() {
    std::wstring dir = exe_dir();
    UpdateInfo info = fetch_latest_release();
    if (!info.ok) return 1;

    std::string baseline;
    if (!read_state(dir, baseline)) baseline = INST_VERSION;
    if (info.num <= update_build_num(baseline) || info.assets.empty()) return 0;

    std::vector<Placed> placed;
    if (install(info, dir, placed) != 0) return 1;
    write_state(dir, info.tag);
    return 0;
}

// ---------- 图形界面 ----------

static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static IDXGISwapChain*         g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;

static const ImVec4 ACCENT   = ImVec4(0.45f, 0.40f, 0.95f, 1.0f);
static const ImVec4 ACCENT_H = ImVec4(0.55f, 0.50f, 1.00f, 1.0f);
static const ImVec4 OK_GREEN = ImVec4(0.30f, 0.85f, 0.45f, 1.0f);
static const ImVec4 ERR_RED  = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
static const ImVec4 BG_DARK  = ImVec4(0.075f, 0.08f, 0.10f, 1.0f);

enum UiState {
    ST_CHECKING,     // 正在查询 GitHub
    ST_LATEST,       // 已是最新
    ST_AVAILABLE,    // 有新版本，等待用户确认
    ST_DOWNLOADING,  // 下载替换中
    ST_DONE,         // 全部完成
    ST_FAILED        // 出错
};

static std::atomic<int> g_ui{ ST_CHECKING };
static UpdateInfo  g_info;             // 查询到的最新版本
static std::string g_baseline;         // 已安装基准（state 文件或编译版本）
static std::string g_err;              // 错误信息
static std::wstring g_dir;

static std::mutex  g_pmtx;
static std::string g_pfile;            // 当前下载的文件名
static std::atomic<int> g_pindex{ 0 }, g_pcount{ 0 };
static std::atomic<unsigned long long> g_pdone{ 0 }, g_ptotal{ 0 };
static std::vector<Placed> g_placed;

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

static void progress_cb(void*, unsigned long long done, unsigned long long total) {
    g_pdone = done;
    g_ptotal = total;
}

static void start_check() {
    update_check_reset();
    update_check_async();
    g_ui = ST_CHECKING;
}

static void start_download() {
    g_ui = ST_DOWNLOADING;
    g_placed.clear();
    g_pindex = 0;
    g_pcount = (int)g_info.assets.size();
    g_pdone = g_ptotal = 0;
    g_err.clear();
    std::thread([] {
        for (int i = 0; i < (int)g_info.assets.size(); ++i) {
            const UpdateAsset& a = g_info.assets[i];
            g_pindex = i + 1;
            { std::lock_guard<std::mutex> lk(g_pmtx); g_pfile = a.name; }
            g_pdone = g_ptotal = 0;
            if (install_asset(a, g_dir, g_placed, progress_cb, nullptr) != 0) {
                g_err = "下载或替换失败：" + a.name + "（请关闭游戏和启动器后重试）";
                g_ui = ST_FAILED;
                return;
            }
        }
        write_state(g_dir, g_info.tag);
        g_ui = ST_DONE;
    }).detach();
}

static void poll_check() {
    if (g_ui != ST_CHECKING || !update_check_done()) return;
    UpdateInfo info;
    update_copy(info);
    if (!info.ok) {
        g_err = "检查更新失败：无法连接 GitHub";
        g_ui = ST_FAILED;
        return;
    }
    g_info = std::move(info);
    long long have = update_build_num(g_baseline);
    g_ui = (g_info.num <= have || g_info.assets.empty()) ? ST_LATEST : ST_AVAILABLE;
}

static void fmt_size(unsigned long long b, char* out, size_t n) {
    if (b >= 1024ull * 1024 * 1024)
        snprintf(out, n, "%.2f GB", b / 1073741824.0);
    else if (b >= 1024ull * 1024)
        snprintf(out, n, "%.1f MB", b / 1048576.0);
    else if (b >= 1024)
        snprintf(out, n, "%.1f KB", b / 1024.0);
    else
        snprintf(out, n, "%llu B", b);
}

static void status_dot(ImU32 color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(p.x + 7, p.y + ImGui::GetTextLineHeight() * 0.55f), 5, color);
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::SameLine(20);
}

static void draw_ui() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1
                    ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
    ImGui::TextColored(ACCENT_H, "InStart 更新");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("自动构建版更新工具");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    UiState ui = (UiState)g_ui.load();

    // 状态行
    char ver[128];
    switch (ui) {
    case ST_CHECKING: {
        const char* dots[] = { "", ".", "..", "..." };
        int k = ((int)(ImGui::GetTime() * 2.0)) % 4;
        snprintf(ver, sizeof ver, "正在检查更新%s", dots[k]);
        status_dot(IM_COL32(150, 152, 160, 255));
        ImGui::TextUnformatted(ver);
        break;
    }
    case ST_LATEST:
        status_dot(IM_COL32(77, 217, 115, 255));
        ImGui::Text("已是最新版本（%s）", INST_VERSION);
        break;
    case ST_AVAILABLE:
        status_dot(IM_COL32(140, 128, 242, 255));
        ImGui::Text("发现新版本：%s", g_info.tag.c_str());
        break;
    case ST_DOWNLOADING:
        status_dot(IM_COL32(140, 128, 242, 255));
        ImGui::Text("正在下载（%d / %d）", g_pindex.load(), g_pcount.load());
        break;
    case ST_DONE:
        status_dot(IM_COL32(77, 217, 115, 255));
        ImGui::TextUnformatted("更新完成，重启游戏或启动器后生效");
        break;
    case ST_FAILED:
        status_dot(IM_COL32(242, 89, 89, 255));
        ImGui::TextColored(ERR_RED, "%s", g_err.c_str());
        break;
    }

    ImGui::TextDisabled("当前版本 %s", INST_VERSION);
    if (ui == ST_AVAILABLE)
        ImGui::TextDisabled("最新版本 %s", g_info.tag.c_str());

    ImGui::Spacing();

    float bottomH = 52.f;
    if (ui == ST_AVAILABLE) {
        ImGui::TextDisabled("更新内容");
        ImGui::BeginChild("##notes", ImVec2(0, -bottomH), true);
        if (g_info.notes.empty()) ImGui::TextDisabled("（无更新说明）");
        else ImGui::TextWrapped("%s", g_info.notes.c_str());
        ImGui::EndChild();
    } else if (ui == ST_DOWNLOADING) {
        unsigned long long done = g_pdone.load(), total = g_ptotal.load();
        char a[32] = "?", b[32] = "?";
        fmt_size(done, a, sizeof a);
        if (total > 0) fmt_size(total, b, sizeof b);
        else snprintf(b, sizeof b, "未知大小");

        char fname[128];
        { std::lock_guard<std::mutex> lk(g_pmtx);
          snprintf(fname, sizeof fname, "%s", g_pfile.c_str()); }
        ImGui::TextDisabled("%s  %s / %s", fname, a, b);

        float frac = total > 0 ? (float)((double)done / total) : 0.f;
        char overlay[32] = "";
        if (total > 0) snprintf(overlay, sizeof overlay, "%.0f%%", frac * 100.f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ACCENT);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), total > 0 ? overlay : nullptr);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("下载完成后自动替换文件，可随时关闭窗口取消");
    } else if (ui == ST_DONE) {
        ImGui::TextDisabled("已更新 %d 个文件", (int)g_placed.size());
        ImGui::BeginChild("##files", ImVec2(0, -bottomH), true);
        for (const Placed& p : g_placed) {
            const wchar_t* fn = wcsrchr(p.final.c_str(), L'\\');
            fn = fn ? fn + 1 : p.final.c_str();
            char name[128];
            WideCharToMultiByte(CP_UTF8, 0, fn, -1, name, sizeof name,
                                nullptr, nullptr);
            ImGui::BulletText("%s", name);
        }
        ImGui::EndChild();
    } else if (ui == ST_FAILED) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextDisabled("可点击下方“重试”再次检查");
    }

    // 底部按钮（右对齐）
    float h = 36.f, w1 = 110.f, w2 = 140.f, gap = 10.f;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - h);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x
                         - w1 - gap - w2);

    if (ui == ST_AVAILABLE) {
        if (ImGui::Button("打开下载页", ImVec2(w1, h)))
            ShellExecuteW(nullptr, L"open",
                          utf8_to_wide(g_info.page).c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ACCENT_H);
        if (ImGui::Button("立即更新", ImVec2(w2, h))) start_download();
        ImGui::PopStyleColor(2);
    } else if (ui == ST_CHECKING || ui == ST_DOWNLOADING) {
        ImGui::BeginDisabled(true);
        ImGui::Button(ui == ST_CHECKING ? "检查中" : "下载中", ImVec2(w2, h));
        ImGui::EndDisabled();
    } else {
        if (ui != ST_DONE) {
            if (ImGui::Button("重试", ImVec2(w1, h))) start_check();
            ImGui::SameLine();
        }
        if (ImGui::Button("关闭", ImVec2(w2, h)))
            PostQuitMessage(0);
    }

    ImGui::End();
}

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

static void apply_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.f;
    s.ChildRounding = 8.f;
    s.FrameRounding = 6.f;
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
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.30f, 0.31f, 0.38f, 1.0f);
    c[ImGuiCol_Separator]       = ImVec4(0.22f, 0.23f, 0.28f, 1.0f);
    c[ImGuiCol_PlotHistogram]   = ACCENT;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    if (wcsstr(GetCommandLineW(), L"--silent"))
        return run_silent();

    g_dir = exe_dir();
    if (!read_state(g_dir, g_baseline)) g_baseline = INST_VERSION;

    WNDCLASSEXW wc = { sizeof wc, CS_CLASSDC, wnd_proc, 0, 0, inst,
                       nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr,
                       L"InStartUpdater", nullptr };
    RegisterClassExW(&wc);

    RECT r = { 0, 0, 560, 430 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"InStart 更新", style,
        (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - h) / 2,
        w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    // 深色标题栏（Win10 1809+，老系统自动忽略）
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    COLORREF caption = 0x001A1413;  // 与 BG_DARK 一致
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof caption);

    if (!create_device(hwnd)) { cleanup_device(); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    apply_style();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    ImFontConfig fc;
    fc.FontNo = 0;
    ImFont* cn = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 18.f, &fc,
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    ImFontConfig fc2;
    fc2.FontNo = 0;
    ImFont* title = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 26.f, &fc2,
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    if (!cn) io.Fonts->AddFontDefault();
    (void)title;

    start_check();

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

        poll_check();

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
