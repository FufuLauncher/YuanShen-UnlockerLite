#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"

namespace FpsUnlock
{
    // Unity Application.get_targetFrameRate：无参返回 int。
    // 本体是 `mov eax,[帧率全局]; ret`，全模块只有一个入口。
    using FnGetTargetFrameRate = int(__fastcall*)();

    // 装了就一直挂着，是否钳制由配置里的 FpsGetterClamp 决定。
    Hooks::Hook<FnGetTargetFrameRate>& GetTargetFrameRateHook();
    int __fastcall DetourGetTargetFrameRate();

    bool Init();

    void Apply();

    void Uninit();
}

// 运行期改目标帧率；与配置热重载等价。
// 0 或负数表示不限帧，内部折算成 999 再写入（写 0/负数会让游戏闪退）。
// 返回 0 表示帧率全局地址不可用（写入未生效）。
extern "C" __declspec(dllexport) int SetFps(int fps);
extern "C" __declspec(dllexport) int GetFps(void);
