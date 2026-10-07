#pragma once
#include <string>
#include <vector>
#include <atomic>

struct UpdateAsset {
    std::string name;
    std::string url;
};

struct UpdateInfo {
    bool        ok = false;
    std::string tag;
    long long   num = 0;
    std::string notes;
    std::string page;
    std::vector<UpdateAsset> assets;
};

typedef void (*DownloadProgress)(void* user, unsigned long long done,
                                 unsigned long long total);

UpdateInfo fetch_latest_release();
long long  update_build_num(const std::string& tag);
bool       download_file(const std::string& url, const std::wstring& path,
                         DownloadProgress cb = nullptr, void* user = nullptr);

std::wstring utf8_to_wide(const std::string& s);

void update_check_async();
void update_check_reset();
bool update_check_done();
bool update_copy(UpdateInfo& out);
