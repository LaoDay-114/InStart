// ============================================================
// InStart 菜单：坐标 HUD + 已启用功能列表（右上角，彩虹字）
//              + 注入提示 + 版本不兼容提示 + ImGui 主界面（含按键绑定）
// ============================================================
#include <cstdio>
#include <cmath>
#include <imgui.h>
#include "config.h"
#include "hooks.h"

// 时间流动的彩虹色（类似主流客户端的 Rainbow ArrayList）
static ImU32 rainbow_color(int idx, float speed = 1.2f, float sat = 0.85f, float val = 1.0f) {
    float hue = fmodf((float)ImGui::GetTime() * speed * 0.15f + idx * 0.12f, 1.0f);
    if (hue < 0) hue += 1.f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, sat, val, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

// 画一条带半透明背景的文字，返回占用高度
static float draw_chip(ImDrawList* dl, ImFont* font, float x, float y, const char* text, ImU32 color) {
    ImVec2 ts = font->CalcTextSizeA(17.0f, FLT_MAX, 0, text);
    const float padX = 7.0f, padY = 3.0f;
    dl->AddRectFilled(ImVec2(x - padX, y - padY),
                      ImVec2(x + ts.x + padX, y + ts.y + padY),
                      IM_COL32(8, 10, 16, 180), 4.0f);
    dl->AddRectFilled(ImVec2(x - padX, y - padY),
                      ImVec2(x - padX + 2.5f, y + ts.y + padY), color, 4.0f);
    dl->AddText(font, 17.0f, ImVec2(x, y), color, text);
    return ts.y + padY * 2 + 3.0f;
}

// 在屏幕顶部居中画提示横幅（带背景）
static void draw_banner(ImDrawList* dl, ImFont* font, const char* text, ImU32 color) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 ts = font->CalcTextSizeA(20.0f, FLT_MAX, 0, text);
    float x = (io.DisplaySize.x - ts.x) * 0.5f;
    float y = 20.0f;
    const float padX = 14.f, padY = 8.f;
    dl->AddRectFilled(ImVec2(x - padX, y - padY),
                      ImVec2(x + ts.x + padX, y + ts.y + padY),
                      IM_COL32(8, 10, 16, 220), 6.0f);
    dl->AddRectFilled(ImVec2(x - padX, y - padY),
                      ImVec2(x - padX + 3.f, y + ts.y + padY), color, 6.0f);
    dl->AddText(font, 20.0f, ImVec2(x, y), color, text);
}

void menu_draw() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    ImGuiIO& io = ImGui::GetIO();

    // ---- 版本不兼容提示（常驻红字，优先级最高）----
    if (!g_cfg.versionOk) {
        char buf[128];
        snprintf(buf, sizeof buf, "InStart：版本不兼容（当前 %s，仅支持 1.21 ~ 1.21.11）",
                 g_state.version[0] ? g_state.version : "未知");
        draw_banner(dl, font, buf, IM_COL32(255, 80, 80, 255));
    }

    // ---- 注入完成提示（绿色横幅，几秒后淡出）----
    if (!g_state.injectShown && g_state.jniReady) {
        g_state.injectShown = true;
    }
    static float injectShowTime = 0.f;
    if (g_state.injectShown && injectShowTime == 0.f)
        injectShowTime = (float)ImGui::GetTime();
    if (injectShowTime > 0.f) {
        float age = (float)ImGui::GetTime() - injectShowTime;
        if (age < 4.0f) { // 显示 4 秒
            int alpha = age > 3.0f ? (int)((4.0f - age) * 255) : 255;
            draw_banner(dl, font, "InStart 注入完成", IM_COL32(80, 255, 120, alpha));
        }
    }

    // ---- 左上角：坐标 HUD（带背景）----
    if (g_cfg.hud && g_cfg.versionOk) {
        char buf[160];
        if (g_state.inGame)
            snprintf(buf, sizeof buf, "InStart  |  X: %.1f  Y: %.1f  Z: %.1f",
                     g_state.px, g_state.py, g_state.pz);
        else
            snprintf(buf, sizeof buf, "InStart  |  未进入世界");
        draw_chip(dl, font, 16, 12, buf, rainbow_color(0));
    }

    // ---- 右上角：已启用功能列表（彩虹 ArrayList）----
    if (g_cfg.versionOk) {
        struct { bool on; const char* name; } feats[] = {
            { g_cfg.fly,        "飞行" },
            { g_cfg.speed,      "加速" },
            { g_cfg.fullbright, "全亮" },
            { g_cfg.esp,        "实体透视" },
            { g_cfg.noFall,     "无摔落" },
        };
        float y = 12.0f;
        int   idx = 0;
        for (auto& f : feats) {
            if (!f.on) continue;
            ImVec2 ts = font->CalcTextSizeA(17.0f, FLT_MAX, 0, f.name);
            float x = io.DisplaySize.x - ts.x - 16.0f;
            y += draw_chip(dl, font, x, y, f.name, rainbow_color(idx));
            ++idx;
        }
    }

    if (!g_cfg.showMenu) return;

    // ---- 主菜单 ----
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(90, 90), ImGuiCond_FirstUseEver);

    ImGui::Begin("InStart", nullptr, ImGuiWindowFlags_NoCollapse);
    {
        char keyName[32];
        ImGui::TextDisabled("%s 开关菜单  |  仅限单机使用",
                            hooks_vk_name(g_cfg.bind[BIND_MENU], keyName, sizeof keyName));
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("#tabs")) {
        if (ImGui::BeginTabItem("移动")) {
            ImGui::Checkbox("飞行", &g_cfg.fly);
            if (g_cfg.fly) {
                ImGui::SliderFloat("飞行速度倍率", &g_cfg.flySpeed, 0.5f, 8.0f, "%.1fx");
                ImGui::SameLine(); ImGui::TextDisabled("(含下降)");
            }
            ImGui::Checkbox("地面加速", &g_cfg.speed);
            if (g_cfg.speed)
                ImGui::SliderFloat("速度倍率", &g_cfg.speedMult, 1.1f, 5.0f, "%.1fx");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("视觉")) {
            ImGui::Checkbox("全亮 (Fullbright)", &g_cfg.fullbright);
            ImGui::SameLine(); ImGui::TextDisabled("亮度拉满，关掉恢复");
            ImGui::Checkbox("实体透视 ESP", &g_cfg.esp);
            ImGui::SameLine(); ImGui::TextDisabled("发光轮廓，穿墙可见");
            if (g_cfg.esp) {
                ImGui::Checkbox("仅生物（忽略掉落物/矿车）", &g_cfg.espMobsOnly);
            }
            ImGui::Checkbox("坐标 HUD", &g_cfg.hud);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("保护")) {
            ImGui::Checkbox("无摔落伤害", &g_cfg.noFall);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("按键")) {
            ImGui::TextDisabled("点击右侧按钮后按下新按键即可修改");
            ImGui::Separator();

            struct BindItem { const char* name; int action; };
            static const BindItem items[] = {
                { "呼出/隐藏菜单", BIND_MENU },
                { "飞行",          BIND_FLY },
                { "地面加速",      BIND_SPEED },
                { "全亮",          BIND_FULLBRIGHT },
                { "实体透视",      BIND_ESP },
                { "无摔落",        BIND_NOFALL },
                { "坐标 HUD",      BIND_HUD },
            };

            int waiting = hooks_get_bind_waiting();
            for (int i = 0; i < (int)(sizeof items / sizeof items[0]); ++i) {
                int a = items[i].action;
                ImGui::Text("%s", items[i].name);
                ImGui::SameLine(220);

                char keyName[32];
                hooks_vk_name(g_cfg.bind[a], keyName, sizeof keyName);

                if (waiting == a) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.60f, 0.80f, 1.0f));
                    if (ImGui::Button("请按键...", ImVec2(120, 0))) {
                        hooks_set_bind_waiting(-1); // 再点一次取消
                    }
                    ImGui::PopStyleColor();
                } else {
                    char label[64];
                    snprintf(label, sizeof label, "%s##%d", keyName, a);
                    if (ImGui::Button(label, ImVec2(120, 0))) {
                        hooks_set_bind_waiting(a);
                    }
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("状态")) {
            ImGui::Text("JNI 初始化: %s", g_state.jniReady ? "完成" : "进行中...");
            ImGui::Text("游戏状态:   %s", g_state.inGame ? "已进入世界" : "未进入世界");
            ImGui::Text("游戏版本:   %s", g_state.version[0] ? g_state.version : "未知");
            ImGui::Separator();
            if (g_cfg.versionOk)
                ImGui::TextDisabled("版本兼容：1.21 ~ 1.21.11");
            else
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "版本不兼容，功能已停用");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
