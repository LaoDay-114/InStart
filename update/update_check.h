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
    long long   num = 0;       // 提交计数
    std::string notes;
    std::string page;
    std::vector<UpdateAsset> assets;
};

UpdateInfo fetch_latest_release();
long long  update_build_num(const std::string& tag);
bool       download_file(const std::string& url, const std::wstring& path);

std::wstring utf8_to_wide(const std::string& s);

void update_check_async();
bool update_check_done();
bool update_copy(UpdateInfo& out);
