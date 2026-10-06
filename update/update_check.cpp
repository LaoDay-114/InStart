#include <windows.h>
#include <winhttp.h>
#include <cstring>
#include <mutex>
#include <thread>
#include "update_check.h"

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static bool http_get(const std::wstring& host, const std::wstring& path,
                     const std::wstring& headers, std::string& body,
                     DownloadProgress cb = nullptr, void* user = nullptr) {
    bool ok = false;
    HINTERNET session = WinHttpOpen(L"InStart", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    WinHttpSetTimeouts(session, 12000, 12000, 15000, 30000);
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);

    HINTERNET connect = WinHttpConnect(session, host.c_str(),
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect
        ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             WINHTTP_FLAG_SECURE)
        : nullptr;

    if (request
        && WinHttpSendRequest(request,
                              headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                              (DWORD)headers.size(),
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(request, nullptr)) {

        DWORD code = 0, len = sizeof code;
        if (WinHttpQueryHeaders(request,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                                WINHTTP_NO_HEADER_INDEX)
            && code == 200) {
            unsigned long long total = 0, got = 0;
            DWORD clen = 0, clenSize = sizeof clen;
            if (WinHttpQueryHeaders(request,
                                    WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &clen, &clenSize,
                                    WINHTTP_NO_HEADER_INDEX))
                total = clen;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(request, &avail)) break;
                if (avail == 0) { ok = true; break; }
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, &chunk[0], avail, &read) || read == 0)
                    break;
                chunk.resize(read);
                body += chunk;
                got += read;
                if (cb) cb(user, got, total);
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

static bool split_url(const std::string& url, std::string& host, std::string& path) {
    if (url.rfind("https://", 0) != 0) return false;
    const char* h = url.c_str() + 8;
    const char* slash = strchr(h, '/');
    if (!slash) { host = h; path = "/"; }
    else { host.assign(h, slash); path = slash; }
    return true;
}

static void put_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_json_string(const std::string& s, size_t i,
                             std::string& out, size_t& end) {
    ++i;
    while (i < s.size()) {
        char c = s[i];
        if (c == '"') { end = i + 1; return true; }
        if (c != '\\') { out += c; ++i; continue; }

        ++i;
        if (i >= s.size()) return false;
        char e = s[i];
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'u': {
            if (i + 4 >= s.size()) return false;
            unsigned cp = 0;
            for (int k = 1; k <= 4; ++k) {
                int h = hex_val(s[i + k]);
                if (h < 0) return false;
                cp = cp * 16 + h;
            }
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size()
                && s[i + 1] == '\\' && s[i + 2] == 'u') {
                unsigned lo = 0;
                bool good = true;
                for (int k = 3; k <= 6; ++k) {
                    int h = hex_val(s[i + k]);
                    if (h < 0) { good = false; break; }
                    lo = lo * 16 + h;
                }
                if (good && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
            }
            put_utf8(out, cp);
            break;
        }
        default: out += e;
        }
        ++i;
    }
    return false;
}

static size_t skip_ws(const std::string& s, size_t p) {
    while (p < s.size()) {
        char c = s[p];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++p;
    }
    return p;
}

static bool get_string_field(const std::string& s, size_t from, const char* key,
                             std::string& val, size_t& next) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    size_t p = s.find(pat, from);
    if (p == std::string::npos) return false;
    p = skip_ws(s, p + strlen(pat));
    if (p >= s.size() || s[p] != '"') return false;
    return read_json_string(s, p, val, next);
}

static UpdateInfo parse_release(const std::string& js) {
    UpdateInfo info;
    size_t nx = 0;
    if (!get_string_field(js, 0, "tag_name", info.tag, nx))
        return info;

    info.num = update_build_num(info.tag);
    get_string_field(js, 0, "html_url", info.page, nx);
    get_string_field(js, 0, "body", info.notes, nx);

    size_t p = js.find("\"assets\":");
    if (p != std::string::npos) {
        p = skip_ws(js, p + 9);
        if (p < js.size() && js[p] == '[') {
            size_t e = p + 1, depth = 1;
            bool inStr = false;
            while (e < js.size() && depth) {
                char c = js[e];
                if (inStr) {
                    if (c == '\\') ++e;
                    else if (c == '"') inStr = false;
                } else if (c == '"') inStr = true;
                else if (c == '[') ++depth;
                else if (c == ']') --depth;
                ++e;
            }

            size_t cur = p + 1;
            for (;;) {
                std::string name, url;
                size_t nx = 0;
                if (!get_string_field(js, cur, "name", name, nx) || nx >= e) break;
                if (!get_string_field(js, nx, "browser_download_url", url, nx) || nx >= e)
                    break;
                if (!name.empty() && url.rfind("https://", 0) == 0)
                    info.assets.push_back({ name, url });
                cur = nx;
            }
        }
    }

    info.ok = true;
    return info;
}

UpdateInfo fetch_latest_release() {
    static const std::wstring headers =
        L"User-Agent: InStart\r\nAccept: application/vnd.github+json\r\n";
    std::string body;
    if (!http_get(L"api.github.com",
                  L"/repos/LaoDay-114/InStart/releases/latest",
                  headers, body))
        return {};
    return parse_release(body);
}

bool download_file(const std::string& url, const std::wstring& path,
                   DownloadProgress cb, void* user) {
    std::string host, path8;
    if (!split_url(url, host, path8)) return false;

    std::string body;
    if (!http_get(utf8_to_wide(host), utf8_to_wide(path8),
                  L"User-Agent: InStart\r\n", body, cb, user))
        return false;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = body.empty() ? TRUE
                           : WriteFile(h, body.data(), (DWORD)body.size(), &written, nullptr);
    CloseHandle(h);
    return ok && written == body.size();
}

long long update_build_num(const std::string& tag) {
    if (tag.rfind("build-", 0) != 0) return 0;
    size_t d = tag.find('-', 6);
    if (d == std::string::npos || tag.find('-', d + 1) != std::string::npos)
        return 0;  // 旧标签 build-日期-时间-SHA 有三段，忽略
    long long num = 0;
    for (size_t i = 6; i < d; ++i) {
        char c = tag[i];
        if (c < '0' || c > '9') return 0;
        num = num * 10 + c - '0';
    }
    return d + 1 < tag.size() ? num : 0;
}

static std::mutex     g_mtx;
static UpdateInfo     g_info;
static std::atomic<bool> g_done{ false };

void update_check_async() {
    std::thread([] {
        UpdateInfo info = fetch_latest_release();
        std::lock_guard<std::mutex> lk(g_mtx);
        g_info = std::move(info);
        g_done.store(true);
    }).detach();
}

void update_check_reset() { g_done.store(false); }

bool update_check_done() { return g_done.load(); }

bool update_copy(UpdateInfo& out) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_done.load()) return false;
    out = g_info;
    return true;
}
