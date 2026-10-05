// ============================================================
// InStartInjector：定位 Minecraft(Java 版) 窗口，
// 用 LoadLibraryW + CreateRemoteThread 把 InStart.dll 注入 javaw.exe
// ============================================================
#include <windows.h>
#include <cstdio>
#include <cstring>

static BOOL CALLBACK find_mc_window(HWND h, LPARAM lp) {
    char cls[64] = {}, title[256] = {};
    GetClassNameA(h, cls, sizeof cls);
    GetWindowTextA(h, title, sizeof title);
    bool isGLFW = strstr(cls, "GLFW") != nullptr;
    bool titleMatch = strstr(title, "Minecraft") != nullptr
                   || strstr(title, "Lunar") != nullptr
                   || strstr(title, "Badlion") != nullptr
                   || strstr(title, "Feather") != nullptr;
    if (isGLFW && titleMatch) {
        *(HWND*)lp = h;
        return FALSE;
    }
    return TRUE;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("[InStartInjector] 启动...\n");

    // 1. 找 Minecraft 窗口
    HWND hwnd = nullptr;
    EnumWindows(find_mc_window, (LPARAM)&hwnd);
    if (!hwnd) {
        printf("[!] 未找到 Minecraft 窗口，请先启动游戏\n");
        system("pause");
        return 1;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    printf("[+] 找到 Minecraft 窗口, PID=%lu\n", pid);

    // 2. 定位同目录下的 InStart.dll
    wchar_t selfDir[MAX_PATH];
    GetModuleFileNameW(nullptr, selfDir, MAX_PATH);
    wchar_t* slash = wcsrchr(selfDir, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wchar_t dllPath[MAX_PATH];
    wcscpy(dllPath, selfDir);
    wcscat(dllPath, L"InStart.dll");
    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES) {
        printf("[!] 找不到 InStart.dll\n");
        system("pause");
        return 1;
    }
    printf("[+] DLL 路径就绪\n");

    // 3. 打开游戏进程
    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProc) {
        printf("[!] OpenProcess 失败（err=%lu），请以管理员身份运行\n", GetLastError());
        system("pause");
        return 1;
    }
    printf("[+] OpenProcess 成功\n");

    // 4. 写入 DLL 路径 -> 远程线程调用 LoadLibraryW
    SIZE_T bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        printf("[!] VirtualAllocEx 失败（err=%lu）\n", GetLastError());
        CloseHandle(hProc);
        system("pause");
        return 1;
    }
    if (!WriteProcessMemory(hProc, remote, dllPath, bytes, nullptr)) {
        printf("[!] WriteProcessMemory 失败（err=%lu）\n", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        system("pause");
        return 1;
    }
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    HANDLE th = CreateRemoteThread(hProc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) {
        printf("[!] CreateRemoteThread 失败（err=%lu）\n", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        system("pause");
        return 1;
    }
    WaitForSingleObject(th, 5000);

    DWORD exitCode = 0;
    GetExitCodeThread(th, &exitCode);
    if (exitCode == 0) {
        printf("[!] DLL 加载失败（LoadLibraryW 返回 0）：可能缺少依赖或被杀软隔离\n");
        CloseHandle(th);
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        system("pause");
        return 1;
    }

    printf("[+] 注入完成！切回游戏按快捷键呼出菜单\n");
    CloseHandle(th);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return 0;
}
