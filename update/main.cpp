#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "update_check.h"

#ifndef INST_VERSION
#define INST_VERSION "dev"
#endif

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

static int install(const UpdateInfo& info, const std::wstring& dir,
                   std::vector<Placed>& placed) {
    for (const UpdateAsset& a : info.assets) {
        if (!valid_name(a.name)) continue;

        std::wstring wname = utf8_to_wide(a.name);
        std::wstring finalPath = dir + wname;
        std::wstring tmpPath = finalPath + L".new";
        std::wstring oldPath = finalPath + L".old";

        DeleteFileW(oldPath.c_str());

        HCURSOR oldCursor = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        bool got = download_file(a.url, tmpPath);
        SetCursor(oldCursor);
        if (!got) {
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
    }
    return placed.empty() && !info.assets.empty() ? 1 : 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    bool silent = wcsstr(GetCommandLineW(), L"--silent") != nullptr;

    std::wstring dir = exe_dir();
    UpdateInfo info = fetch_latest_release();

    if (!info.ok) {
        if (!silent)
            MessageBoxW(nullptr, L"检查更新失败：网络不可用或无法连接 GitHub",
                        L"InStart 更新", MB_ICONWARNING);
        return 1;
    }

    std::string baseline;
    if (!read_state(dir, baseline)) baseline = INST_VERSION;
    long long have = update_build_num(baseline);

    if (info.num <= have || info.assets.empty()) {
        if (!silent)
            MessageBoxW(nullptr, L"已是最新版本", L"InStart 更新", MB_ICONINFORMATION);
        return 0;
    }

    if (!silent) {
        std::string shortNotes = info.notes;
        if (shortNotes.size() > 800) shortNotes.resize(800);
        std::wstring msg = L"发现新版本：" + utf8_to_wide(info.tag) + L"\n\n"
                         + utf8_to_wide(shortNotes)
                         + L"\n\n是否立即更新？（更新后需重启游戏/启动器生效）";
        if (MessageBoxW(nullptr, msg.c_str(), L"InStart 更新",
                        MB_ICONQUESTION | MB_YESNO) != IDYES)
            return 0;
    }

    std::vector<Placed> placed;
    if (install(info, dir, placed) != 0) {
        MessageBoxW(nullptr, L"下载或替换文件失败，请关闭相关程序后重试",
                    L"InStart 更新", MB_ICONERROR);
        return 1;
    }

    write_state(dir, info.tag);

    if (!silent)
        MessageBoxW(nullptr, L"更新完成，重启游戏或启动器后生效",
                    L"InStart 更新", MB_ICONINFORMATION);
    return 0;
}
