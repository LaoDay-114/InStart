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

// 下载进度：done/total 字节，total 可能为 0（服务器未返回长度）
typedef void (*DownloadProgress)(void* user, unsigned long long done,
                                 unsigned long long total);

UpdateInfo fetch_latest_release();
long long  update_build_num(const std::string& tag);
bool       download_file(const std::string& url, const std::wstring& path,
                         DownloadProgress cb = nullptr, void* user = nullptr);

std::wstring utf8_to_wide(const std::string& s);

// 后台线程检查更新，结果通过 update_copy 取走
void update_check_async();
void update_check_reset();     // 允许重新发起检查
bool update_check_done();
bool update_copy(UpdateInfo& out);
