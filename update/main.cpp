#include "eui_neo.h"

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "update_check.h"

#ifndef INST_VERSION
#define INST_VERSION "dev"
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

int eui_app_run();

namespace app {
namespace {

using eui::Color;

const Color kAccent    = components::theme::defaultPrimary();
const Color kAccentHi  = components::theme::color(0.35f, 0.55f, 0.95f);
const Color kOkGreen   = components::theme::color(0.30f, 0.85f, 0.45f);
const Color kErrRed    = components::theme::color(0.95f, 0.35f, 0.35f);
const Color kText      = components::theme::color(1.00f, 1.00f, 1.00f);
const Color kMuted     = components::theme::withOpacity(components::theme::dark().text, 0.55f);


std::wstring exe_dir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    return p.substr(0, s + 1);
}

bool valid_name(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    if (n.find("..") != std::string::npos) return false;
    for (char c : n)
        if (c == '/' || c == '\\') return false;
    return true;
}

bool read_state(const std::wstring& dir, std::string& tag) {
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

void write_state(const std::wstring& dir, const std::string& tag) {
    FILE* f = _wfopen((dir + L"InStartUpdate.state").c_str(), L"wb");
    if (!f) return;
    fprintf(f, "installed=%s\n", tag.c_str());
    fclose(f);
}

struct Placed {
    std::wstring final, old;
};

int install_asset(const UpdateAsset& a, const std::wstring& dir,
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

    DeleteFileW(oldPath.c_str());
    placed.push_back({ finalPath, moved ? oldPath : L"" });
    return 0;
}

int install(const UpdateInfo& info, const std::wstring& dir,
            std::vector<Placed>& placed) {
    for (const UpdateAsset& a : info.assets)
        if (install_asset(a, dir, placed) != 0) return 1;
    return placed.empty() && !info.assets.empty() ? 1 : 0;
}


int run_silent() {
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


enum UiState {
    ST_CHECKING,
    ST_LATEST,
    ST_AVAILABLE,
    ST_DOWNLOADING,
    ST_DONE,
    ST_FAILED
};

std::atomic<int> g_ui{ ST_CHECKING };
UpdateInfo  g_info;
std::string g_baseline;
std::string g_err;
std::wstring g_dir;

std::mutex  g_pmtx;
std::string g_pfile;
std::atomic<int> g_pindex{ 0 }, g_pcount{ 0 };
std::atomic<unsigned long long> g_pdone{ 0 }, g_ptotal{ 0 };
std::vector<Placed> g_placed;

void progress_cb(void*, unsigned long long done, unsigned long long total) {
    g_pdone = done;
    g_ptotal = total;
    requestUpdate();
}

void start_check() {
    g_ui = ST_CHECKING;
    core::async::restart("update.check",
        [] { return fetch_latest_release(); },
        [](core::async::Result<UpdateInfo> r) {
            if (!r.ok || !r.value.ok) {
                g_err = "检查更新失败：无法连接 GitHub";
                g_ui = ST_FAILED;
                return;
            }
            g_info = std::move(r.value);
            long long have = update_build_num(g_baseline);
            g_ui = (g_info.num <= have || g_info.assets.empty())
                       ? ST_LATEST : ST_AVAILABLE;
        });
}

void start_download() {
    g_ui = ST_DOWNLOADING;
    g_placed.clear();
    g_pindex = 0;
    g_pcount = (int)g_info.assets.size();
    g_pdone = g_ptotal = 0;
    g_err.clear();
    core::async::restart("update.download", [] {
        for (int i = 0; i < (int)g_info.assets.size(); ++i) {
            const UpdateAsset& a = g_info.assets[i];
            g_pindex = i + 1;
            { std::lock_guard<std::mutex> lk(g_pmtx); g_pfile = a.name; }
            g_pdone = g_ptotal = 0;
            requestUpdate();
            if (install_asset(a, g_dir, g_placed, progress_cb, nullptr) != 0) {
                g_err = "下载或替换失败：" + a.name + "（请关闭游戏和启动器后重试）";
                return false;
            }
        }
        write_state(g_dir, g_info.tag);
        return true;
    }, [](core::async::Result<bool> r) {
        g_ui = (r.ok && r.value) ? ST_DONE : ST_FAILED;
    });
}

void fmt_size(unsigned long long b, char* out, size_t n) {
    if (b >= 1024ull * 1024 * 1024)
        snprintf(out, n, "%.2f GB", b / 1073741824.0);
    else if (b >= 1024ull * 1024)
        snprintf(out, n, "%.1f MB", b / 1048576.0);
    else if (b >= 1024)
        snprintf(out, n, "%.1f KB", b / 1024.0);
    else
        snprintf(out, n, "%llu B", b);
}

void close_window() {
    HWND hwnd = FindWindowW(nullptr, L"InStart 更新");
    if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

void dark_title_bar_once() {
    static bool done = false;
    if (done) return;
    done = true;
    HWND hwnd = FindWindowW(nullptr, L"InStart 更新");
    if (!hwnd) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    COLORREF caption = RGB(0x1A, 0x1A, 0x1F);
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof caption);
}

Color dot_color(int ui) {
    switch (ui) {
    case ST_LATEST:
    case ST_DONE:   return kOkGreen;
    case ST_FAILED: return kErrRed;
    case ST_AVAILABLE:
    case ST_DOWNLOADING: return kAccent;
    default:        return components::theme::color(0.59f, 0.60f, 0.63f);
    }
}

std::string status_text(int ui) {
    switch (ui) {
    case ST_CHECKING:    return "正在检查更新…";
    case ST_LATEST:      return "已是最新版本（" + std::string(INST_VERSION) + "）";
    case ST_AVAILABLE:   return "发现新版本：" + g_info.tag;
    case ST_DOWNLOADING: return "正在下载（" + std::to_string(g_pindex.load()) +
                                 " / " + std::to_string(g_pcount.load()) + "）";
    case ST_DONE:        return "更新完成，重启游戏或启动器后生效";
    default:             return g_err;
    }
}

void build_content(eui::Ui& ui, float w, float h) {
    const int state = g_ui.load();
    if (state == ST_AVAILABLE) {
        ui.text("notes.label")
            .text("更新内容")
            .fontSize(13.0f)
            .color(kMuted)
            .build();
        components::card(ui, "card.notes")
            .width(w)
            .height(h)
            .radius(12.0f)
            .padding(14.0f)
            .content([&] {
                components::scrollView(ui, "notes.scroll")
                    .size(w - 28.0f, h - 28.0f)
                    .content([&](eui::Ui& sui, float contentW, float) {
                        sui.text("notes.text")
                            .width(contentW)
                            .text(g_info.notes.empty() ? "（无更新说明）" : g_info.notes)
                            .fontSize(13.0f)
                            .lineHeight(19.0f)
                            .color(g_info.notes.empty() ? kMuted : kText)
                            .wrap(true)
                            .build();
                    })
                    .build();
            })
            .build();
    } else if (state == ST_DOWNLOADING) {
        unsigned long long done = g_pdone.load(), total = g_ptotal.load();
        char a[32], b[32];
        fmt_size(done, a, sizeof a);
        if (total > 0) fmt_size(total, b, sizeof b);
        else snprintf(b, sizeof b, "未知大小");
        std::string fname;
        { std::lock_guard<std::mutex> lk(g_pmtx); fname = g_pfile; }

        ui.text("dl.file")
            .size(w, 18.0f)
            .text(fname + "  " + a + " / " + b)
            .fontSize(12.0f)
            .color(kMuted)
            .build();
        const float frac = total > 0 ? (float)((double)done / total) : 0.0f;
        components::progress(ui, "dl.progress")
            .size(w, 10.0f)
            .value(frac)
            .build();
        ui.text("dl.hint")
            .size(w, 18.0f)
            .text("下载完成后自动替换文件，可随时关闭窗口取消")
            .fontSize(12.0f)
            .color(kMuted)
            .build();
    } else if (state == ST_DONE) {
        ui.text("done.count")
            .text("已更新 " + std::to_string(g_placed.size()) + " 个文件")
            .fontSize(13.0f)
            .color(kMuted)
            .build();
        components::card(ui, "card.files")
            .width(w)
            .height(h)
            .radius(12.0f)
            .padding(14.0f)
            .content([&] {
                components::scrollView(ui, "files.scroll")
                    .size(w - 28.0f, h - 28.0f)
                    .gap(4.0f)
                    .content([&](eui::Ui& sui, float, float) {
                        for (size_t i = 0; i < g_placed.size(); ++i) {
                            const wchar_t* fn = wcsrchr(g_placed[i].final.c_str(), L'\\');
                            fn = fn ? fn + 1 : g_placed[i].final.c_str();
                            char name[128];
                            WideCharToMultiByte(CP_UTF8, 0, fn, -1, name,
                                                sizeof name, nullptr, nullptr);
                            sui.text("done.file." + std::to_string(i))
                                .text(std::string("• ") + name)
                                .fontSize(13.0f)
                                .color(kText)
                                .build();
                        }
                    })
                    .build();
            })
            .build();
    } else if (state == ST_FAILED) {
        ui.text("failed.hint")
            .text("可点击下方“重试”再次检查")
            .fontSize(13.0f)
            .color(kMuted)
            .build();
    }
}

void build_footer(eui::Ui& ui, float w) {
    const int state = g_ui.load();
    ui.row("footer")
        .size(w, 40.0f)
        .gap(10.0f)
        .justifyContent(eui::Align::END)
        .alignItems(eui::Align::CENTER)
        .content([&] {
            if (state == ST_AVAILABLE) {
                components::button(ui, "btn.page")
                    .size(110.0f, 36.0f)
                    .text("打开下载页")
                    .fontSize(14.0f)
                    .theme(components::theme::dark(), false)
                    .onClick([] {
                        ShellExecuteW(nullptr, L"open",
                                      utf8_to_wide(g_info.page).c_str(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
                    })
                    .build();
                components::button(ui, "btn.update")
                    .size(140.0f, 36.0f)
                    .text("立即更新")
                    .fontSize(14.0f)
                    .theme(components::theme::dark(), true)
                    .onClick([] { start_download(); })
                    .build();
            } else if (state == ST_CHECKING || state == ST_DOWNLOADING) {
                components::button(ui, "btn.busy")
                    .size(140.0f, 36.0f)
                    .text(state == ST_CHECKING ? "检查中" : "下载中")
                    .fontSize(14.0f)
                    .theme(components::theme::dark(), true)
                    .disabled(true)
                    .build();
            } else {
                if (state != ST_DONE) {
                    components::button(ui, "btn.retry")
                        .size(110.0f, 36.0f)
                        .text("重试")
                        .fontSize(14.0f)
                        .theme(components::theme::dark(), false)
                        .onClick([] { start_check(); })
                        .build();
                }
                components::button(ui, "btn.close")
                    .size(140.0f, 36.0f)
                    .text("关闭")
                    .fontSize(14.0f)
                    .theme(components::theme::dark(), true)
                    .onClick([] { close_window(); })
                    .build();
            }
        })
        .build();
}

}

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("InStart 更新")
        .pageId("updater")
        .clearColor(components::theme::dark().background)
        .windowSize(560, 460)
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
        g_dir = exe_dir();
        if (!read_state(g_dir, g_baseline)) g_baseline = INST_VERSION;
        start_check();
    }

    const float pad = 24.0f;
    const float w = screen.width - pad * 2.0f;
    const float gap = 12.0f;
    const float headerH = 34.0f;
    const float statusH = 24.0f;
    const float verH = 18.0f;
    const float footerH = 40.0f;
    const int state = g_ui.load();
    const float contentH = screen.height - pad * 2.0f - headerH - statusH - verH -
                           footerH - gap * 4.0f - 18.0f;

    ui.column("root")
        .size(screen.width, screen.height)
        .padding(pad)
        .gap(gap)
        .content([&] {
            ui.row("header").size(w, headerH).gap(12.0f)
                .alignItems(eui::Align::CENTER)
                .content([&] {
                    ui.text("header.title")
                        .text("InStart 更新")
                        .fontSize(24.0f)
                        .color(kAccentHi)
                        .build();
                    ui.text("header.sub")
                        .text("自动构建版更新工具")
                        .fontSize(12.0f)
                        .color(kMuted)
                        .build();
                })
                .build();

            ui.row("status").size(w, statusH).gap(10.0f)
                .alignItems(eui::Align::CENTER)
                .content([&] {
                    ui.rect("status.dot")
                        .size(10.0f, 10.0f)
                        .color(dot_color(state))
                        .radius(5.0f)
                        .build();
                    ui.text("status.text")
                        .text(status_text(state))
                        .fontSize(14.0f)
                        .color(state == ST_FAILED ? kErrRed : kText)
                        .build();
                })
                .build();

            std::string verLine = "当前版本 " + std::string(INST_VERSION);
            if (state == ST_AVAILABLE) verLine += "    最新版本 " + g_info.tag;
            ui.text("version")
                .size(w, verH)
                .text(verLine)
                .fontSize(12.0f)
                .color(kMuted)
                .build();

            build_content(ui, w, contentH);
            build_footer(ui, w);
        })
        .build();
}

}

int main() {
    if (wcsstr(GetCommandLineW(), L"--silent"))
        return app::run_silent();
    return eui_app_run();
}
