#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace ProcessInfo
{
    struct MainModule
    {
        uintptr_t base    = 0;
        size_t    size    = 0;
        uint16_t  machine = 0;
        wchar_t   path[MAX_PATH] = {};
        wchar_t   version[64]    = {};
    };

    struct SelfModule
    {
        HMODULE handle = nullptr;
        wchar_t path[MAX_PATH] = {};
        wchar_t dir[MAX_PATH]  = {};
    };

    struct Snapshot
    {
        DWORD      pid       = 0;
        DWORD      tid       = 0;
        bool       wow64     = false;
        uint64_t   startTime = 0;
        MainModule main      = {};
        SelfModule self      = {};
    };

    bool Capture(HMODULE selfHandle = nullptr);

    // 本进程的窗口当前是否在前台。
    // 取不到前景窗口等异常情况一律返回 true（宁可当作"在前台"，
    // 免得因一次查询失败就把玩家按后台策略处理）。
    bool IsForeground();

    bool SelfSiblingPath(const wchar_t* ext, wchar_t* out, size_t outChars);

    const Snapshot& Get();
    void Log();
}
