// injector.cpp —— 把 trainer.dll 注入 Twin Shot Deluxe 进程
//
// 用法:
//   injector.exe                         → 注入同目录 trainer.dll 到 "Twin Shot Deluxe.exe"
//   injector.exe <进程名> <DLL路径>       → 自定义
//
// 流程:等进程出现(最长 60s)→ 分配远程内存写入 DLL 全路径 →
//       远程线程调用 LoadLibraryW → 等待加载完成。
//
// 编译(MSVC x64):      cl /O2 /EHsc injector\injector.cpp /Fe:injector.exe shell32.lib
// 编译(MinGW-w64):     g++ -O2 -std=c++17 injector\injector.cpp -o injector.exe -lshell32
//
// 注意:入口用标准 main() + CommandLineToArgvW 取宽字符参数。
//   不要用 wmain —— 那是 MSVC 扩展,g++ 下会报 undefined reference to `WinMain`。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <string>

static DWORD FindPid(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static void WaitPid(const wchar_t* exeName) {
    wprintf(L"[*] waiting for %ls ...\n", exeName);
    for (int i = 0; i < 600; ++i) {                     // 最长 60 秒
        DWORD pid = FindPid(exeName);
        if (pid) { wprintf(L"[+] found %ls (pid %lu)\n", exeName, pid); return; }
        Sleep(100);
    }
    wprintf(L"[!] %ls not found (timeout)\n", exeName);
}

static bool Inject(DWORD pid, const wchar_t* dllPath) {
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) { wprintf(L"[!] OpenProcess failed, err=%lu (需要管理员权限?)\n", GetLastError()); return false; }

    SIZE_T bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) { wprintf(L"[!] VirtualAllocEx failed, err=%lu\n", GetLastError()); CloseHandle(h); return false; }

    if (!WriteProcessMemory(h, mem, dllPath, bytes, nullptr)) {
        wprintf(L"[!] WriteProcessMemory failed, err=%lu\n", GetLastError());
        CloseHandle(h); return false;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE loadLib =
        (LPTHREAD_START_ROUTINE)(void*)GetProcAddress(k32, "LoadLibraryW");
    if (!loadLib) { CloseHandle(h); return false; }

    HANDLE t = CreateRemoteThread(h, nullptr, 0, loadLib, mem, 0, nullptr);
    if (!t) { wprintf(L"[!] CreateRemoteThread failed, err=%lu\n", GetLastError()); CloseHandle(h); return false; }

    WaitForSingleObject(t, 10000);
    DWORD exitCode = 0;
    GetExitCodeThread(t, &exitCode);
    CloseHandle(t);
    CloseHandle(h);

    if (exitCode == 0) { wprintf(L"[!] LoadLibraryW failed (exit 0), see trainer log\n"); return false; }
    wprintf(L"[+] DLL loaded (module handle %p)\n", (void*)(uintptr_t)exitCode);
    return true;
}

int main() {
    // 标准入口拿到的是 ANSI argv(中文路径可能乱码),
    // 所以用 GetCommandLineW + CommandLineToArgvW 拿宽字符参数,两个编译器行为一致。
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { wprintf(L"[!] CommandLineToArgvW failed\n"); return 1; }

    const wchar_t* procName = (argc > 1) ? argv[1] : L"Twin Shot Deluxe.exe";

    std::wstring dllPath;
    if (argc > 2) {
        dllPath = argv[2];
    } else {
        wchar_t self[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, self, MAX_PATH);        // injector.exe 自己的路径
        std::wstring dir = self;
        size_t slash = dir.find_last_of(L'\\');
        if (slash != std::wstring::npos) dir = dir.substr(0, slash);
        dllPath = dir + L"\\trainer.dll";
    }
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"[!] DLL not found: %ls\n", dllPath.c_str());
        LocalFree(argv);
        return 1;
    }

    WaitPid(procName);
    DWORD pid = FindPid(procName);
    if (!pid) { LocalFree(argv); return 1; }

    wprintf(L"[*] injecting %ls -> pid %lu\n", dllPath.c_str(), pid);
    bool ok = Inject(pid, dllPath.c_str());
    LocalFree(argv);
    return ok ? 0 : 1;
}
