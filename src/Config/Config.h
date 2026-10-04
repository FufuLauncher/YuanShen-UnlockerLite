#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Config
{
    struct Values
    {
        // [Fps]
        bool  fpsEnabled        = true;    // Value，帧率解锁开关
        // [TargetFps]
        int   targetFps         = 240;     // Value，0 或负数 = 不限帧（内部折算成 999）
        // [FpsGetterClamp]
        bool  fpsGetterClamp    = false;   // Value，把 getter 返回值钳到官方上限

        // [Fov]
        bool  fovEnabled        = false;   // Value，默认关闭
        // [TargetFov]
        int   targetFov         = 60;      // Value，有效范围 [30, 179]
        // [FovRecover]
        bool  fovRecoverEnabled = true;    // Value，呼出鼠标/失焦时平滑恢复原生视场角

        // [WaitDebugger] —— 只有 Debug 版会看这个值，Release 下读了也没人用。
        // 默认 false：普通注入立刻装 hook，不做任何等待。
        bool  debugWaitAttach   = false;   // Value，注入后等调试器附加
    };

    // 首次读取：解析 ini 路径 + 读文件 + 发布。路径来自 ProcessInfo 记录的本 DLL
    // 位置，所以必须在 ProcessInfo::Capture() 之后调；有文件 IO，只能在 worker 线程。
    void Init();

    // 重新读一次并发布。读不到就整份不动（保持上一份继续跑）。
    // 供热重载用，在 watcher 线程调用：只做文件 IO + 一次加锁发布，不做别的。
    void Reload();

    // 取一份当前配置的拷贝。Init 之前拿到的是内置默认值。
    // 可跨线程调用；与 Reload() 之间用 SRWLOCK 串行。
    Values Snapshot();

    // ini 的完整路径（诊断用，也用来交给 Watcher 监听）。Init 之前为空串。
    const wchar_t* IniPath();
}
