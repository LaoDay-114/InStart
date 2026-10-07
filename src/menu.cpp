#include <cstdio>
#include <cmath>
#include <string>
#include <imgui.h>
#include "config.h"
#include "hooks.h"
#include <shellapi.h>
#include "update_check.h"

#ifndef INST_VERSION
#define INST_VERSION "dev"
#endif

static ImU32 rainbow_color(int idx, float speed = 1.2f, float sat = 0.85f, float val = 1.0f) {
    float hue = fmodf((float)ImGui::GetTime() * speed * 0.15f + idx * 0.12f, 1.0f);
    if (hue < 0) hue += 1.f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, sat, val, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

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

static void draw_banner(ImDrawList* dl, ImFont* font, const char* text, ImU32 color,
                        float y = 20.0f) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 ts = font->CalcTextSizeA(20.0f, FLT_MAX, 0, text);
    float x = (io.DisplaySize.x - ts.x) * 0.5f;
    const float padX = 14.f, padY = 8.f;
    dl->AddRectFilled(ImVec2(x - padX, y - padY),
                      ImVec2(x + ts.x + padX, y + ts.y + padY),
                      IM_COL32(8, 10, 16, 220), 6.0f);
    dl->AddRectFilled(ImVec2(x - padX, y - padY),
                      ImVec2(x - padX + 3.f, y + ts.y + padY), color, 6.0f);
    dl->AddText(font, 20.0f, ImVec2(x, y), color, text);
}

static std::wstring dll_dir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleW(L"InStart.dll"), buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    return p.substr(0, s + 1);
}

void menu_draw() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    ImGuiIO& io = ImGui::GetIO();

    if (!g_state.injectShown && g_state.jniReady) {
        g_state.injectShown = true;
    }
    static float injectShowTime = 0.f;
    if (g_state.injectShown && injectShowTime == 0.f)
        injectShowTime = (float)ImGui::GetTime();
    if (injectShowTime > 0.f) {
        float age = (float)ImGui::GetTime() - injectShowTime;
        if (age < 4.0f) {
            int alpha = age > 3.0f ? (int)((4.0f - age) * 255) : 255;
            draw_banner(dl, font, "InStart 注入完成", IM_COL32(80, 255, 120, alpha));
        }
    }

    static UpdateInfo s_update;
    static bool s_updateGot = false;
    if (!s_updateGot && update_check_done()) {
        update_copy(s_update);
        s_updateGot = true;
    }

    long long curNum = update_build_num(INST_VERSION);
    bool updateAvailable = s_updateGot && s_update.ok && s_update.num > curNum;

    static float updateBannerAt = -1.f;
    if (updateAvailable && updateBannerAt < 0.f)
        updateBannerAt = (float)ImGui::GetTime();
    if (updateBannerAt > 0.f && ImGui::GetTime() - updateBannerAt < 12.f) {
        char buf[96];
        snprintf(buf, sizeof buf, "发现新版本 %s，打开菜单 -> 状态", s_update.tag.c_str());
        draw_banner(dl, font, buf, IM_COL32(56, 113, 224, 255), 64.0f);
    }

    if (g_cfg.hud) {
        char buf[160];
        if (g_state.inGame)
            snprintf(buf, sizeof buf, "InStart  |  X: %.1f  Y: %.1f  Z: %.1f",
                     g_state.px, g_state.py, g_state.pz);
        else
            snprintf(buf, sizeof buf, "InStart  |  未进入世界");
        draw_chip(dl, font, 16, 12, buf, rainbow_color(0));
    }

    {
        struct { bool on; const char* name; } feats[] = {
            { g_cfg.fly,        "飞行" },
            { g_cfg.speed,      "加速" },
            { g_cfg.fullbright, "全亮" },
            { g_cfg.esp,        "实体透视" },
            { g_cfg.noFall,     "无摔落" },
            { g_cfg.killaura,   "杀戮光环" },
            { g_cfg.autoTotem,  "自动图腾" },
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

    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(90, 90), ImGuiCond_FirstUseEver);

    ImGui::Begin("InStart", nullptr, ImGuiWindowFlags_NoCollapse);
    {
        char keyName[32];
        ImGui::TextDisabled("%s 开关菜单",
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
        if (ImGui::BeginTabItem("战斗")) {
            ImGui::Checkbox("杀戮光环", &g_cfg.killaura);
            ImGui::SameLine(); ImGui::TextDisabled("自动攻击最近生物");
            if (g_cfg.killaura) {
                ImGui::SliderFloat("攻击范围", &g_cfg.auraRange, 1.5f, 5.0f, "%.1f 格");
                ImGui::Checkbox("排除玩家", &g_cfg.kaExcludePlayers);
                ImGui::SameLine(160);
                ImGui::Checkbox("排除生物", &g_cfg.kaExcludeMobs);
            }
            ImGui::Checkbox("自动图腾", &g_cfg.autoTotem);
            ImGui::SameLine(); ImGui::TextDisabled("副手无图腾时自动从背包补上");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("保护")) {
            ImGui::Checkbox("无摔落伤害", &g_cfg.noFall);
            ImGui::SameLine(); ImGui::TextDisabled("Packet 模式（参考 Meteor）");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("按键")) {
            ImGui::TextDisabled("点击右侧按钮后按下新按键即可修改（按 Esc/Delete 清除绑定）");
            ImGui::Separator();

            struct BindItem { const char* name; int action; };
            static const BindItem items[] = {
                { "呼出/隐藏菜单", BIND_MENU },
                { "飞行",          BIND_FLY },
                { "地面加速",      BIND_SPEED },
                { "全亮",          BIND_FULLBRIGHT },
                { "实体透视",      BIND_ESP },
                { "无摔落",        BIND_NOFALL },
                { "坐标 HUD", BIND_HUD },
                { "杀戮光环", BIND_KILLAURA },
                { "自动图腾", BIND_TOTEM },
            };

            int waiting = hooks_get_bind_waiting();
            for (int i = 0; i < (int)(sizeof items / sizeof items[0]); ++i) {
                int a = items[i].action;
                ImGui::Text("%s", items[i].name);
                ImGui::SameLine(220);

                char keyName[32];
                hooks_vk_name(g_cfg.bind[a], keyName, sizeof keyName);

                if (waiting == a) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.44f, 0.88f, 1.0f));
                    if (ImGui::Button("请按键...", ImVec2(120, 0))) {
                        hooks_set_bind_waiting(-1);
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
            ImGui::Text("当前版本: %s", INST_VERSION);

            if (!s_updateGot) {
                ImGui::TextDisabled("正在检查更新...");
            } else if (!s_update.ok) {
                ImGui::TextDisabled("更新检查失败（网络不可用）");
            } else if (updateAvailable) {
                ImGui::TextColored(ImVec4(0.22f, 0.44f, 0.88f, 1.0f),
                                   "发现新版本: %s", s_update.tag.c_str());
                ImGui::BeginChild("##notes", ImVec2(-1, 110), true);
                if (s_update.notes.empty()) ImGui::TextDisabled("(无更新说明)");
                else ImGui::TextWrapped("%s", s_update.notes.c_str());
                ImGui::EndChild();

                if (ImGui::Button("打开下载页"))
                    ShellExecuteW(nullptr, L"open",
                                  utf8_to_wide(s_update.page).c_str(), nullptr,
                                  nullptr, SW_SHOWNORMAL);
                ImGui::SameLine();
                std::wstring dir = dll_dir();
                if (ImGui::Button("立即更新"))
                    ShellExecuteW(nullptr, L"open",
                                  (dir + L"InStartUpdateManager.exe").c_str(),
                                  nullptr, dir.c_str(), SW_SHOWNORMAL);
                ImGui::SameLine();
                ImGui::TextDisabled("更新后重启游戏生效");
            } else {
                ImGui::Text("已是最新版本");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
