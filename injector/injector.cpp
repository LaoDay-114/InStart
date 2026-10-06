// ============================================================
// InStartInjector（CLI 后端）：供 InStart.exe 启动器调用
//   InStartInjector.exe --list            列出检测到的游戏实例
//   InStartInjector.exe --inject <pid>    向指定进程注入 InStart.dll
//   InStartInjector.exe                   兼容旧用法：自动找第一个实例注入
// 退出码：0=成功  1=业务失败（错误信息在输出中）  2=注入器自身崩溃
// 全程 SEH 保护：崩溃时输出崩溃码并以 2 退出，不弹系统错误框。
// ============================================================
#include <windows.h>
#include <cstdio>
#include <cstring>

#include "game_instances.h"

// 注入流程（被 SEH 包裹）
static int do_inject(DWORD pid) {
    printf("[+] 目标进程 PID=%lu\n", pid);

    // 1. 定位同目录下的 InStart.dll
    wchar_t selfDir[MAX_PATH];
    GetModuleFileNameW(nullptr, selfDir, MAX_PATH);
    wchar_t* slash = wcsrchr(selfDir, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wchar_t dllPath[MAX_PATH];
    wcscpy(dllPath, selfDir);
    wcscat(dllPath, L"InStart.dll");
    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES) {
        printf("[ERR] 找不到 InStart.dll（应与注入器放在同一目录）\n");
        return 1;
    }
    printf("[+] DLL 路径就绪\n");

    // 2. 打开游戏进程
    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProc) {
        printf("[ERR] OpenProcess 失败（err=%lu）：请以管理员身份运行，或进程已退出\n",
               GetLastError());
        return 1;
    }
    printf("[+] OpenProcess 成功\n");

    // 3. 写入 DLL 路径 -> 远程线程调用 LoadLibraryW
    SIZE_T bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProc, nullptr, bytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        printf("[ERR] VirtualAllocEx 失败（err=%lu）\n", GetLastError());
        CloseHandle(hProc);
        return 1;
    }
    if (!WriteProcessMemory(hProc, remote, dllPath, bytes, nullptr)) {
        printf("[ERR] WriteProcessMemory 失败（err=%lu）\n", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    HANDLE th = CreateRemoteThread(hProc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) {
        printf("[ERR] CreateRemoteThread 失败（err=%lu）\n", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }
    WaitForSingleObject(th, 5000);

    DWORD exitCode = 0;
    GetExitCodeThread(th, &exitCode);
    CloseHandle(th);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hProc);

    if (exitCode == 0) {
        printf("[ERR] DLL 加载失败（LoadLibraryW 返回 0）：可能缺少依赖或被杀软隔离\n");
        return 1;
    }

    printf("[OK] 注入完成\n");
    return 0;
}

static int do_list() {
    GameInstance items[16];
    int n = scan_game_instances(items, 16);
    printf("[LIST] count=%d\n", n);
    for (int i = 0; i < n; ++i)
        printf("[INSTANCE] pid=%lu title=%s\n", items[i].pid, items[i].title);
    return 0;
}

// 崩溃码 -> 可读描述
static const char* seh_name(unsigned code) {
    switch (code) {
    case 0xC0000005: return "访问冲突（读写了无效内存）";
    case 0xC000001D: return "非法指令";
    case 0xC0000094: return "整数除零";
    case 0xC00000FD: return "栈溢出";
    case 0xE0434352: return ".NET 异常";
    default: return nullptr;
    }
}

static int seh_body(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "--list") == 0)
        return do_list();

    if (argc >= 3 && strcmp(argv[1], "--inject") == 0)
        return do_inject((DWORD)strtoul(argv[2], nullptr, 10));

    if (argc == 1) { // 兼容旧用法：自动注入第一个实例
        GameInstance items[16];
        int n = scan_game_instances(items, 16);
        if (n == 0) {
            printf("[ERR] 未找到 Minecraft 窗口，请先启动游戏\n");
            return 1;
        }
        printf("[+] 找到窗口 PID=%lu：%s\n", items[0].pid, items[0].title);
        return do_inject(items[0].pid);
    }

    printf("[ERR] 用法：InStartInjector.exe [--list | --inject <pid>]\n");
    return 1;
}

// 顶层未处理异常过滤器：注入器崩溃时输出崩溃码并以 2 退出（MinGW 无 __try/__except）
static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep) {
    unsigned code = ep->ExceptionRecord->ExceptionCode;
    const char* name = seh_name(code);
    // 同时写 stdout/stderr：VEH 场景下 stderr 无缓冲更可靠
    fprintf(stdout, "[CRASH] 注入器崩溃：code=0x%08lX%s%s\n",
            code, name ? " " : "", name ? name : "");
    fflush(stdout); fflush(stderr);
    ExitProcess(2);
    return EXCEPTION_EXECUTE_HANDLER; // 不会到达
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    // 屏蔽系统崩溃弹窗，交给异常过滤器处理
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash_filter);
    AddVectoredExceptionHandler(1, crash_filter);

    return seh_body(argc, argv);
}
