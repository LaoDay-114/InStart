// ============================================================
// InStart Hook 层：
//  - MinHook 挂 wglSwapBuffers -> 帧间绘制 ImGui
//  - 子类化 GLFW 窗口 -> 菜单打开时接管输入
//  - 快捷键轮询（含菜单呼出键、功能快捷键、按键绑定等待状态）
// ============================================================
#include <windows.h>
#include <cstdio>
#include <imgui.h>
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"
#include <MinHook.h>

#include "hooks.h"
#include "config.h"
#include "menu.h"
#include "jni/mc.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT CALLBACK in_wndproc(HWND, UINT, WPARAM, LPARAM); // 前向声明

static BOOL (WINAPI* o_wglSwapBuffers)(HDC) = nullptr;
static WNDPROC o_wndproc = nullptr;
static HWND    g_hwnd = nullptr;
static bool    g_imguiReady = false;

// 按键绑定等待状态：-1 = 不在等待；>=0 = 等待为 g_cfg.bind[i] 捕获按键
static int   g_bindWaiting = -1;
static bool  g_bindPrev[256] = {}; // 各 VK 码上一帧状态，用于边沿触发

// ---- ImGui + 窗口初始化（首次 wglSwapBuffers 时执行，处于游戏渲染线程）----
static bool imgui_once_init(HDC hdc) {
    g_hwnd = WindowFromDC(hdc);
    if (!g_hwnd) return false;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;      // 不落盘 imgui.ini
    io.MouseDrawCursor = g_cfg.showMenu;

    // 加载带中文的字体（微软雅黑优先，逐级回退）
    const char* fonts[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    bool fontLoaded = false;
    for (const char* f : fonts) {
        if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES) {
            ImFont* font = io.Fonts->AddFontFromFileTTF(
                f, 17.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (font) { fontLoaded = true; break; }
        }
    }
    (void)fontLoaded; // 加载失败则退回默认字体（仅英文可显示）

    // 主题：深色 + 圆角 + 青色强调
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 8.f; st.FrameRounding = 5.f;
    st.GrabRounding = 5.f;   st.PopupRounding = 6.f;
    st.WindowPadding = ImVec2(14, 12); st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(10, 8);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.06f, 0.07f, 0.10f, 0.94f);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.10f, 0.14f, 0.20f, 1.00f);
    c[ImGuiCol_Border]          = ImVec4(0.25f, 0.65f, 0.85f, 0.45f);
    c[ImGuiCol_CheckMark]       = ImVec4(0.30f, 0.80f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]      = ImVec4(0.30f, 0.80f, 1.00f, 0.80f);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.45f, 0.90f, 1.00f, 1.00f);
    c[ImGuiCol_Tab]             = ImVec4(0.10f, 0.14f, 0.20f, 1.00f);
    c[ImGuiCol_TabSelected]     = ImVec4(0.16f, 0.34f, 0.46f, 1.00f);
    c[ImGuiCol_Header]          = ImVec4(0.16f, 0.34f, 0.46f, 0.60f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.12f, 0.16f, 0.22f, 1.00f);

    if (!ImGui_ImplWin32_Init(g_hwnd)) return false;
    if (!ImGui_ImplOpenGL3_Init(nullptr)) return false;

    // 子类化游戏窗口
    o_wndproc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)in_wndproc);
    return true;
}

// ---- 消息处理：菜单打开时吞掉游戏输入 ----
LRESULT CALLBACK in_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_imguiReady)
        ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp);

    // 菜单打开时吞掉鼠标/原始输入：点击菜单不会转动视角、误触游戏
    if (g_cfg.showMenu) {
        switch (msg) {
        case WM_INPUT:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:   case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:   case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:   case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
            return 0;
        default: break;
        }
    }
    return o_wndproc ? CallWindowProcW(o_wndproc, h, msg, wp, lp)
                     : DefWindowProcW(h, msg, wp, lp);
}

// ---- wglSwapBuffers Hook：帧间插入我们的渲染 ----
static BOOL WINAPI hk_wglSwapBuffers(HDC hdc) {
    static bool initDone = false;
    if (!initDone) {
        initDone = true;
        g_imguiReady = imgui_once_init(hdc);
    }
    if (g_imguiReady)
        hooks_frame();
    return o_wglSwapBuffers(hdc);
}

// 把 VK 码转成可读名字（用于菜单显示）
const char* hooks_vk_name(int vk, char* buf, size_t len) {
    if (vk <= 0 || vk > 255) { snprintf(buf, len, "未绑定"); return buf; }
    switch (vk) {
    case VK_RMENU:    return "右 Alt";
    case VK_LMENU:    return "左 Alt";
    case VK_RCONTROL: return "右 Ctrl";
    case VK_LCONTROL: return "左 Ctrl";
    case VK_RSHIFT:   return "右 Shift";
    case VK_LSHIFT:   return "左 Shift";
    case VK_SPACE:    return "空格";
    case VK_RETURN:   return "回车";
    case VK_TAB:      return "Tab";
    case VK_CAPITAL:  return "Caps";
    case VK_ESCAPE:   return "Esc";
    case VK_UP:       return "上"; case VK_DOWN: return "下";
    case VK_LEFT:     return "左"; case VK_RIGHT: return "右";
    case VK_INSERT:   return "Insert"; case VK_DELETE: return "Delete";
    case VK_HOME:     return "Home";   case VK_END:    return "End";
    case VK_PRIOR:    return "PageUp"; case VK_NEXT:   return "PageDown";
    case VK_NUMPAD0:  return "小键盘0"; case VK_NUMPAD1: return "小键盘1";
    case VK_NUMPAD2:  return "小键盘2"; case VK_NUMPAD3: return "小键盘3";
    case VK_NUMPAD4:  return "小键盘4"; case VK_NUMPAD5: return "小键盘5";
    case VK_NUMPAD6:  return "小键盘6"; case VK_NUMPAD7: return "小键盘7";
    case VK_NUMPAD8:  return "小键盘8"; case VK_NUMPAD9: return "小键盘9";
    case VK_MULTIPLY: return "小键盘*"; case VK_ADD:     return "小键盘+";
    case VK_SUBTRACT: return "小键盘-"; case VK_DECIMAL: return "小键盘.";
    case VK_DIVIDE:   return "小键盘/";
    case VK_OEM_3:    return "`";
    case VK_OEM_MINUS:return "-";
    case VK_OEM_PLUS: return "=";
    case VK_OEM_4:    return "["; case VK_OEM_6: return "]";
    case VK_OEM_5:    return "\\"; case VK_OEM_1: return ";";
    case VK_OEM_7:    return "'"; case VK_OEM_COMMA: return ",";
    case VK_OEM_PERIOD:return "."; case VK_OEM_2: return "/";
    default:
        if (vk >= 'A' && vk <= 'Z') { snprintf(buf, len, "%c", (char)vk); return buf; }
        if (vk >= '0' && vk <= '9') { snprintf(buf, len, "%c", (char)vk); return buf; }
        snprintf(buf, len, "VK 0x%02X", vk);
        return buf;
    }
}

// 边沿触发检测：vk 从松开变为按下时返回 true
static bool key_edge(int vk) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = down && !g_bindPrev[vk];
    g_bindPrev[vk] = down;
    return edge;
}

// 按键绑定轮询：处理等待绑定 + 常规快捷键
static void poll_keybinds() {
    // 正在等待绑定某个按键
    if (g_bindWaiting >= 0 && g_bindWaiting < BIND_COUNT) {
        // 扫所有可绑定的键：字母、数字、功能键、方向键、小键盘等
        for (int vk = 0x08; vk <= 0xFE; ++vk) {
            // 跳过鼠标键和菜单键自身
            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
            if (key_edge(vk)) {
                g_cfg.bind[g_bindWaiting] = vk;
                g_bindWaiting = -1;
                break;
            }
        }
        return; // 等待绑定时不响应其他快捷键
    }

    // 菜单呼出/隐藏
    if (key_edge(g_cfg.bind[BIND_MENU])) {
        g_cfg.showMenu = !g_cfg.showMenu;
        ImGui::GetIO().MouseDrawCursor = g_cfg.showMenu;
    }

    // 功能快捷键（仅当版本兼容时才有意义）
    if (!g_cfg.versionOk) return;
    if (key_edge(g_cfg.bind[BIND_FLY]))        g_cfg.fly        = !g_cfg.fly;
    if (key_edge(g_cfg.bind[BIND_SPEED]))      g_cfg.speed      = !g_cfg.speed;
    if (key_edge(g_cfg.bind[BIND_FULLBRIGHT])) g_cfg.fullbright = !g_cfg.fullbright;
    if (key_edge(g_cfg.bind[BIND_ESP]))        g_cfg.esp        = !g_cfg.esp;
    if (key_edge(g_cfg.bind[BIND_NOFALL]))     g_cfg.noFall     = !g_cfg.noFall;
    if (key_edge(g_cfg.bind[BIND_HUD]))        g_cfg.hud        = !g_cfg.hud;
}

// 供 menu.cpp 调用：设置/查询按键绑定等待状态
void hooks_set_bind_waiting(int idx) { g_bindWaiting = idx; }
int  hooks_get_bind_waiting()        { return g_bindWaiting; }

void hooks_frame() {
    // 1. 应用功能（写入游戏对象 / 回填坐标）—— 在游戏渲染线程上执行
    mc_apply_features();

    // 2. 快捷键轮询
    poll_keybinds();

    // 3. 绘制 UI
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    menu_draw();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void hooks_init(HMODULE) {
    if (MH_Initialize() != MH_OK) return;

    // 等待 opengl32.dll 就绪（游戏启动后必定加载）
    HMODULE ogl = nullptr;
    for (int i = 0; i < 240 && !(ogl = GetModuleHandleW(L"opengl32.dll")); ++i)
        Sleep(250);
    if (!ogl) return;

    void* target = (void*)GetProcAddress(ogl, "wglSwapBuffers");
    if (!target) return;

    if (MH_CreateHook(target, (void*)&hk_wglSwapBuffers, (void**)&o_wglSwapBuffers) != MH_OK)
        return;
    MH_EnableHook(target);
}
