#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "FovUnlock.h"
#include "Config.h"
#include "ExponentialFilter.h"
#include "Logger.h"
#include <atomic>

namespace FovUnlock
{
    static Hooks::Hook<FnSetFieldOfView> g_hook;
    static std::atomic<int>   g_target{ 60 };
    static std::atomic<bool>  g_active{ false };


    static std::atomic<float> g_weightTarget{ 1.0f };

    static std::atomic<bool>  g_recoverEnabled{ true };


    static constexpr float kRecoverTau = 0.08f;


    static Util::ExponentialFilter<float> g_filter;
    static SRWLOCK g_filterLock = SRWLOCK_INIT;

    static HANDLE g_pollThread = nullptr;
    static HANDLE g_pollStop   = nullptr;

    static constexpr DWORD kPollIntervalMs = 16;
    static constexpr DWORD kPollStopWaitMs = 3000;

    static constexpr int kFovFloor = 30;
    static constexpr int kMaxFov = 179;


    static constexpr float kWeightSnap = 0.9999f;

    static int ClampFov(int fov)
    {
        if (fov < kFovFloor) return kFovFloor;
        if (fov > kMaxFov) return kMaxFov;
        return fov;
    }

    Hooks::Hook<FnSetFieldOfView>& SetFieldOfViewHook() { return g_hook; }

  
    static bool IsCursorVisible()
    {
        CURSORINFO ci{};
        ci.cbSize = static_cast<DWORD>(sizeof(ci));
        if (!GetCursorInfo(&ci))
            return false;
        return (ci.flags & CURSOR_SHOWING) != 0;
    }


    static bool IsGameFocused()
    {
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
            return true;

        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        return pid == 0 || pid == GetCurrentProcessId();
    }

    static void StopPollThread()
    {
        if (!g_pollStop)
            return;

        SetEvent(g_pollStop);

        if (g_pollThread) {
            if (WaitForSingleObject(g_pollThread, kPollStopWaitMs) != WAIT_OBJECT_0)
                LOG("Fov", "探测线程未在 %lu ms 内退出", kPollStopWaitMs);
            CloseHandle(g_pollThread);
            g_pollThread = nullptr;
        }

        CloseHandle(g_pollStop);
        g_pollStop = nullptr;
    }


    static DWORD WINAPI PollProc(LPVOID)
    {
        for (;;) {
            const bool uiActive = IsCursorVisible() || !IsGameFocused();

            const float target = (g_recoverEnabled.load(std::memory_order_relaxed) && uiActive)
                                     ? 0.0f
                                     : 1.0f;

            g_weightTarget.store(target, std::memory_order_relaxed);

            if (WaitForSingleObject(g_pollStop, kPollIntervalMs) == WAIT_OBJECT_0)
                break;
        }

        return 0;
    }

    static bool EnsurePollThread()
    {
        if (g_pollThread)
            return true;

        g_pollStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_pollStop) {
            LOG("Fov", "CreateEvent 失败（错误 %lu）", GetLastError());
            return false;
        }

        g_pollThread = CreateThread(nullptr, 0, PollProc, nullptr, 0, nullptr);
        if (!g_pollThread) {
            LOG("Fov", "CreateThread 失败（错误 %lu）", GetLastError());
            CloseHandle(g_pollStop);
            g_pollStop = nullptr;
            return false;
        }
        return true;
    }

    void __fastcall DetourSetFieldOfView(void* self, float value)
    {
        HOOK_INFLIGHT_SCOPE();

     
        const float weightTarget = g_weightTarget.load(std::memory_order_relaxed);

        float weight;
        AcquireSRWLockExclusive(&g_filterLock);
        weight = g_filter.Update(weightTarget);
        ReleaseSRWLockExclusive(&g_filterLock);

        if (weight >= kWeightSnap)
            weight = 1.0f;
        else if (weight <= 1.0f - kWeightSnap)
            weight = 0.0f;

        if (g_active.load(std::memory_order_relaxed) && weight > 0.0f) {
            const float target = static_cast<float>(g_target.load(std::memory_order_relaxed));


            // (kFovFloor, 目标值] 一律顶到目标值；kFovFloor 本身及以下、以及高于
            // 目标值的原样透传。下界取严格不等号，目标值填 30 时条件恒不成立，
            // 整段退化为不改。
            float mapped = value;
            if (value > static_cast<float>(kFovFloor) && value <= target)
                mapped = target;

            value = (weight >= 1.0f) ? mapped : value + (mapped - value) * weight;
        }

        if (FnSetFieldOfView original = g_hook.Original())
            original(self, value);
    }

    // 把当前配置搬到运行态。Init 与热重载共用这一处 ——
    // 重载后 FOV 会立刻按新值工作，不需要重新注入。
    void Apply()
    {
        const Config::Values cfg = Config::Snapshot();

        const int target = ClampFov(cfg.targetFov);
        g_target.store(target, std::memory_order_relaxed);

        g_active.store(cfg.fovEnabled, std::memory_order_relaxed);
        g_recoverEnabled.store(cfg.fovRecoverEnabled, std::memory_order_relaxed);

        LOG("Fov", "已应用：接管=%d 目标 %d（透传下限 %d，上限 %d）界面恢复=%d",
            cfg.fovEnabled ? 1 : 0, target, kFovFloor, kMaxFov,
            cfg.fovRecoverEnabled ? 1 : 0);
    }

    bool Init()
    {
        if (!g_hook.Installed()) {
            LOG_MSG("Fov", "hook 未安装（特征码未命中或安装失败），无法接管");
            return false;
        }

        Apply();

        AcquireSRWLockExclusive(&g_filterLock);
        g_filter.SetTimeConstant(kRecoverTau);
        g_filter.Reset(1.0f);
        ReleaseSRWLockExclusive(&g_filterLock);

        g_weightTarget.store(1.0f, std::memory_order_relaxed);

        if (!EnsurePollThread())
            LOG_MSG("Fov", "界面态探测未启动，呼出鼠标时不会恢复视场角");

        return true;
    }

    void Uninit()
    {

        g_active.store(false, std::memory_order_relaxed);
        StopPollThread();

        g_hook.Detach();
    }
}

extern "C" __declspec(dllexport) int SetFov(int fov)
{
    FovUnlock::g_target.store(FovUnlock::ClampFov(fov), std::memory_order_relaxed);
    return 1;
}

extern "C" __declspec(dllexport) int GetFov(void)
{
    return FovUnlock::g_target.load(std::memory_order_relaxed);
}
