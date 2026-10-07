// Hook 层：MinHook 挂 wglSwapBuffers 做帧渲染，子类化窗口接管菜单输入，
// 并轮询快捷键与绑定等待状态。
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

LRESULT CALLBACK in_wndproc(HWND, UINT, WPARAM, LPARAM);

static BOOL (WINAPI* o_wglSwapBuffers)(HDC) = nullptr;
static WNDPROC o_wndproc = nullptr;
static HWND    g_hwnd = nullptr;
static bool    g_imguiReady = false;

static int   g_bindWaiting = -1; // -1 = 不在等待，否则等待为 bind[i] 捕获按键
static bool  g_bindPrev[256] = {};

static bool imgui_once_init(HDC hdc) {
    g_hwnd = WindowFromDC(hdc);
    if (!g_hwnd) return false;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.MouseDrawCursor = g_cfg.showMenu;

    const char* fonts[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    for (const char* f : fonts) {
        if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES) {
            if (io.Fonts->AddFontFromFileTTF(
                    f, 17.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()))
                break;
        }
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    // EUI-NEO dark 主题移植
    st.WindowRounding = 12.f; st.ChildRounding = 12.f;
    st.FrameRounding = 8.f;   st.GrabRounding = 8.f;
    st.PopupRounding = 10.f;  st.TabRounding = 8.f;
    st.ScrollbarRounding = 12.f;
    st.WindowPadding = ImVec2(16, 14); st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(12, 10);
    st.ScrollbarSize = 8.f;
    st.WindowBorderSize = 0.f;

    const ImVec4 BG      = ImVec4(0.10f, 0.10f, 0.12f, 0.96f);
    const ImVec4 SURF    = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    const ImVec4 SURF_H  = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    const ImVec4 SURF_A  = ImVec4(0.35f, 0.35f, 0.38f, 1.00f);
    const ImVec4 ACC     = ImVec4(0.22f, 0.44f, 0.88f, 1.00f);
    const ImVec4 BORD    = ImVec4(0.30f, 0.30f, 0.30f, 0.60f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]        = BG;
    c[ImGuiCol_ChildBg]         = SURF;
    c[ImGuiCol_PopupBg]         = ImVec4(SURF.x, SURF.y, SURF.z, 0.98f);
    c[ImGuiCol_Border]          = BORD;
    c[ImGuiCol_Text]            = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_TextDisabled]    = ImVec4(1.00f, 1.00f, 1.00f, 0.55f);
    c[ImGuiCol_FrameBg]         = SURF;
    c[ImGuiCol_FrameBgHovered]  = SURF_H;
    c[ImGuiCol_FrameBgActive]   = SURF_A;
    c[ImGuiCol_Button]          = SURF;
    c[ImGuiCol_ButtonHovered]   = SURF_H;
    c[ImGuiCol_ButtonActive]    = SURF_A;
    c[ImGuiCol_Header]          = ImVec4(ACC.x, ACC.y, ACC.z, 0.18f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(ACC.x, ACC.y, ACC.z, 0.32f);
    c[ImGuiCol_HeaderActive]    = ImVec4(ACC.x, ACC.y, ACC.z, 0.45f);
    c[ImGuiCol_TitleBg]         = SURF;
    c[ImGuiCol_TitleBgActive]   = SURF_H;
    c[ImGuiCol_CheckMark]       = ACC;
    c[ImGuiCol_SliderGrab]      = ACC;
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.35f, 0.55f, 0.95f, 1.00f);
    c[ImGuiCol_Tab]             = SURF;
    c[ImGuiCol_TabHovered]      = ImVec4(ACC.x, ACC.y, ACC.z, 0.40f);
    c[ImGuiCol_TabSelected]     = ImVec4(ACC.x, ACC.y, ACC.z, 0.55f);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]   = SURF_H;
    c[ImGuiCol_ScrollbarGrabHovered] = SURF_A;
    c[ImGuiCol_ScrollbarGrabActive]  = SURF_A;
    c[ImGuiCol_Separator]       = BORD;

    if (!ImGui_ImplWin32_Init(g_hwnd)) return false;
    if (!ImGui_ImplOpenGL3_Init(nullptr)) return false;

    o_wndproc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)in_wndproc);
    return true;
}

LRESULT CALLBACK in_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_imguiReady)
        ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp);

    // 菜单打开时吞掉鼠标输入，点击不会转动视角
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

static bool key_edge(int vk) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = down && !g_bindPrev[vk];
    g_bindPrev[vk] = down;
    return edge;
}

static void poll_keybinds() {
    if (g_bindWaiting >= 0 && g_bindWaiting < BIND_COUNT) {
        // Esc / Delete 清除绑定
        if (key_edge(VK_ESCAPE) || key_edge(VK_DELETE)) {
            g_cfg.bind[g_bindWaiting] = 0;
            g_bindWaiting = -1;
            config_save();
            return;
        }
        for (int vk = 0x08; vk <= 0xFE; ++vk) {
            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
            if (key_edge(vk)) {
                g_cfg.bind[g_bindWaiting] = vk;
                g_bindWaiting = -1;
                config_save();
                break;
            }
        }
        return;
    }

    if (key_edge(g_cfg.bind[BIND_MENU])) {
        g_cfg.showMenu = !g_cfg.showMenu;
        ImGui::GetIO().MouseDrawCursor = g_cfg.showMenu;
        if (!g_cfg.showMenu) config_save();
    }

    if (key_edge(g_cfg.bind[BIND_FLY]))        g_cfg.fly        = !g_cfg.fly;
    if (key_edge(g_cfg.bind[BIND_SPEED]))      g_cfg.speed      = !g_cfg.speed;
    if (key_edge(g_cfg.bind[BIND_FULLBRIGHT])) g_cfg.fullbright = !g_cfg.fullbright;
    if (key_edge(g_cfg.bind[BIND_ESP]))        g_cfg.esp        = !g_cfg.esp;
    if (key_edge(g_cfg.bind[BIND_NOFALL]))     g_cfg.noFall     = !g_cfg.noFall;
    if (key_edge(g_cfg.bind[BIND_HUD]))        g_cfg.hud        = !g_cfg.hud;
    if (key_edge(g_cfg.bind[BIND_KILLAURA]))   g_cfg.killaura   = !g_cfg.killaura;
    if (key_edge(g_cfg.bind[BIND_TOTEM]))      g_cfg.autoTotem  = !g_cfg.autoTotem;
}

void hooks_set_bind_waiting(int idx) { g_bindWaiting = idx; }
int  hooks_get_bind_waiting()        { return g_bindWaiting; }

void hooks_frame() {
    mc_apply_features();

    poll_keybinds();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    menu_draw();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void hooks_init(HMODULE) {
    if (MH_Initialize() != MH_OK) return;

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
