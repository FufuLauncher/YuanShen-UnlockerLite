#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ProcessInfo.h"
#include "Logger.h"

namespace ProcessInfo
{
    static Snapshot g_snapshot{};

    static bool ReadPeHeaders(uintptr_t base, MainModule& out)
    {
        __try {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return false;

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return false;

            out.size    = nt->OptionalHeader.SizeOfImage;
            out.machine = nt->FileHeader.Machine;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static void ReadFileVersion(const wchar_t* path, wchar_t* out, size_t outChars)
    {
        DWORD dummy = 0;
        const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
        if (size == 0)
            return;

        unsigned char buf[1024] = {};
        if (size > sizeof(buf))
            return;
        if (!GetFileVersionInfoW(path, 0, size, buf))
            return;

        VS_FIXEDFILEINFO* ffi = nullptr;
        UINT ffiLen = 0;
        if (!VerQueryValueW(buf, L"\\", reinterpret_cast<void**>(&ffi), &ffiLen))
            return;
        if (!ffi || ffiLen < sizeof(VS_FIXEDFILEINFO))
            return;
        if (ffi->dwSignature != 0xFEEF04BD)
            return;

        swprintf_s(out, outChars, L"%u.%u.%u.%u",
                   HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                   HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    }

    static HMODULE ResolveSelf(HMODULE given)
    {
        if (given)
            return given;

        HMODULE h = nullptr;
#pragma warning(push)
#pragma warning(disable : 4191)
        const LPCWSTR self = reinterpret_cast<LPCWSTR>(&ResolveSelf);
#pragma warning(pop)
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           self, &h);
        return h;
    }

    bool Capture(HMODULE selfHandle)
    {
        Snapshot& s = g_snapshot;
        s = Snapshot{};

        s.pid = GetCurrentProcessId();
        s.tid = GetCurrentThreadId();

        BOOL wow = FALSE;
        if (IsWow64Process(GetCurrentProcess(), &wow))
            s.wow64 = (wow != FALSE);

        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            ULARGE_INTEGER li;
            li.LowPart  = created.dwLowDateTime;
            li.HighPart = created.dwHighDateTime;
            s.startTime = li.QuadPart;
        }

        s.main.base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!s.main.base)
            return false;

        GetModuleFileNameW(nullptr, s.main.path, MAX_PATH);
        ReadPeHeaders(s.main.base, s.main);
        ReadFileVersion(s.main.path, s.main.version, ARRAYSIZE(s.main.version));

        s.self.handle = ResolveSelf(selfHandle);
        if (s.self.handle) {
            GetModuleFileNameW(s.self.handle, s.self.path, MAX_PATH);
            wcscpy_s(s.self.dir, s.self.path);
            if (wchar_t* lastSlash = wcsrchr(s.self.dir, L'\\'))
                *lastSlash = 0;
        }

        return true;
    }

    const Snapshot& Get() { return g_snapshot; }

    bool IsForeground()
    {
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
            return true;

        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        // pid 取不到时也当作在前台，与上面同样的保守取向。
        // 这里现取进程 id 而不读快照，免得依赖 Capture 的调用顺序。
        return pid == 0 || pid == GetCurrentProcessId();
    }

    bool SelfSiblingPath(const wchar_t* ext, wchar_t* out, size_t outChars)
    {
        if (!ext || !out || outChars == 0)
            return false;
        out[0] = 0;

        const wchar_t* path = g_snapshot.self.path;
        if (!path[0])
            return false;

        wchar_t buf[MAX_PATH] = {};
        wcscpy_s(buf, path);

        wchar_t* lastSlash = wcsrchr(buf, L'\\');
        wchar_t* base = lastSlash ? lastSlash + 1 : buf;
        if (wchar_t* lastDot = wcsrchr(base, L'.'))
            *lastDot = 0;

        if (wcslen(buf) + wcslen(ext) + 1 > outChars)
            return false;

        wcscpy_s(out, outChars, buf);
        wcscat_s(out, outChars, ext);
        return true;
    }

    void Log()
    {
        const Snapshot& s = g_snapshot;

        LOG("Process", "pid=%lu tid=%lu wow64=%s startTime=%llu",
            s.pid, s.tid, s.wow64 ? "yes" : "no",
            static_cast<unsigned long long>(s.startTime));
        LOG("Process", "main base=%p size=0x%zX machine=0x%04X version=%ls",
            reinterpret_cast<void*>(s.main.base), s.main.size, s.main.machine,
            s.main.version[0] ? s.main.version : L"(unknown)");
        LOG("Process", "main path %ls", s.main.path);

        LOG("Process", "self base=%p path=%ls",
            reinterpret_cast<void*>(s.self.handle), s.self.path);
    }
}
