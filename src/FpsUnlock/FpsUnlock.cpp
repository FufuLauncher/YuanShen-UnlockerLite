#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "FpsUnlock.h"
#include "Config.h"
#include "Game.h"
#include "Logger.h"
#include "Patterns.h"
#include "ProcessInfo.h"
#include "Scanner.h"
#include <atomic>

namespace FpsUnlock
{

    static volatile int* g_fpsAddr = nullptr;

    static std::atomic<int> g_target{ 0 };

    static std::atomic<bool> g_enabled{ false };


    static std::atomic<bool> g_clampEnabled{ false };

    static std::atomic<bool> g_backgroundLimit{ false };

    static constexpr int kGetterClampMax = 120;

    // 后台（游戏窗口不是前台）时的帧率上限。
    static constexpr int kBackgroundFps = 10;

    // "不限帧"统一写成这个值：贴着引擎硬上限 1000，等效不限帧，
    // 但本身是个合法正整数。写 0 / 负数会让游戏闪退，不能落到全局里。
    static constexpr int kUnlimitedFps = 999;

    static constexpr DWORD kWriteIntervalMs = 500;

    static HANDLE g_thread = nullptr;
    
    static HANDLE g_stop = nullptr;

    static constexpr DWORD kThreadStopWaitMs = 3000;

    static Hooks::Hook<FnGetTargetFrameRate> g_getterHook;

    static int NormalizeTarget(int fps)
    {
        // 0 和负数都不能直接写进全局（会让游戏闪退），折算成 kUnlimitedFps。
        if (fps <= 0)
            return kUnlimitedFps;

        if (fps > 1000)
            LOG("Fps", "目标 %d 超过游戏硬上限 1000（sub_1416A6790 里的 fminf），实际只会有 1000", fps);

        return fps;
    }

    Hooks::Hook<FnGetTargetFrameRate>& GetTargetFrameRateHook() { return g_getterHook; }

    int __fastcall DetourGetTargetFrameRate()
    {
        HOOK_INFLIGHT_SCOPE();

        const bool clamp = g_clampEnabled.load(std::memory_order_relaxed);

        FnGetTargetFrameRate original = g_getterHook.Original();
        if (!original)
            return clamp ? kGetterClampMax : kUnlimitedFps;

        const int value = original();

        // 0 / 负数会让游戏闪退，也不是合法的目标帧率，折算成 kUnlimitedFps。
        int out = (value <= 0) ? kUnlimitedFps : value;

        // 开钳制时对外一律不超过上限（折算后的 999 也会一起被压下来）。
        if (clamp && out > kGetterClampMax)
            out = kGetterClampMax;

        return out;
    }

    static uintptr_t ResolveFpsGlobalBySig()
    {
        const uintptr_t hit = Scanner::ScanMainMod(Patterns::Sig::FpsReadInFramePacer);
        if (!hit) {
            LOG_MSG("Fps", "帧率读取指令的特征码未命中");
            return 0;
        }

        const uintptr_t target = Scanner::ResolveRelative(hit, 4, 8);
        if (!target) {
            LOG("Fps", "特征码命中 %p，但 disp32 取不出来（内存不可读）",
                reinterpret_cast<void*>(hit));
            return 0;
        }

        const ProcessInfo::MainModule& main = ProcessInfo::Get().main;
        if (target < main.base || target >= main.base + main.size) {
            LOG("Fps", "签名反推出 %p，落在主模块 [%p, %p) 之外，判为不可信",
                reinterpret_cast<void*>(target),
                reinterpret_cast<void*>(main.base),
                reinterpret_cast<void*>(main.base + main.size));
            return 0;
        }

        LOG("Fps", "特征码命中 %p，反推出帧率全局 %p",
            reinterpret_cast<void*>(hit), reinterpret_cast<void*>(target));
        return target;
    }

    static uintptr_t ResolveFpsGlobal()
    {
        const uintptr_t fromSig = ResolveFpsGlobalBySig();
        const uintptr_t fromRva = Game::Resolve(Patterns::Rva::FpsLimit);

        if (!fromSig) {
            if (fromRva)
                LOG_MSG("Fps", "退回硬编码 RVA 兜底（签名未命中：可能游戏改了那段代码的形状，"
                               "也可能只是这条签名过时）。写入前仍会做可写性校验");
            return fromRva;
        }

        if (fromRva && fromSig != fromRva)
            LOG("Fps", "签名反推出的 %p 与硬编码 RVA 推出的 %p 不一致 —— 游戏更新过，"
                       "RVA 已过期。本次按签名走；核对无误后请更新 Patterns::Rva::FpsLimit",
                reinterpret_cast<void*>(fromSig), reinterpret_cast<void*>(fromRva));

        return fromSig;
    }

    static bool ResolveTarget()
    {
        const uintptr_t addr = ResolveFpsGlobal();
        if (!addr) {
            LOG_MSG("Fps", "帧率全局定位失败（签名与 RVA 都没给出可用地址），放弃写入");
            return false;
        }

        g_fpsAddr = Game::AcquireWritable<int>(addr);
        if (!g_fpsAddr) {
            MEMORY_BASIC_INFORMATION mbi{};
            Game::Query(addr, mbi);
            LOG("Fps", "目标 %p 不可写（State=0x%lX Protect=0x%lX），多半是游戏版本不匹配，放弃写入",
                reinterpret_cast<void*>(addr),
                static_cast<unsigned long>(mbi.State),
                static_cast<unsigned long>(mbi.Protect));
            return false;
        }

        LOG("Fps", "帧率全局 %p（主模块基址 %p + 0x%llX）",
            reinterpret_cast<void*>(addr),
            reinterpret_cast<void*>(Game::ModuleBase()),
            static_cast<unsigned long long>(addr - Game::ModuleBase()));
        return true;
    }

    // 本轮该写进全局的值。后台限帧开且不在前台时压到 kBackgroundFps。
    // 用 min 而不是直接覆盖：目标本身比 kBackgroundFps 还低时（比如填 5）
    // 不该被"限制"反倒抬高帧率。999 表示不限帧，所以也会被压下来。
    static int EffectiveTarget()
    {
        const int target = g_target.load(std::memory_order_relaxed);

        if (g_backgroundLimit.load(std::memory_order_relaxed) && !ProcessInfo::IsForeground())
            return (target < kBackgroundFps) ? target : kBackgroundFps;

        return target;
    }

    static void WriteOnce()
    {
        if (!g_fpsAddr)
            return;

        *g_fpsAddr = EffectiveTarget();
    }

    static DWORD WINAPI WriterProc(LPVOID)
    {
        LOG("Fps", "覆写线程启动：目标 %d（间隔每轮重读配置）",
            g_target.load(std::memory_order_relaxed));

        for (;;) {

            if (g_enabled.load(std::memory_order_relaxed))
                WriteOnce();

            if (WaitForSingleObject(g_stop, kWriteIntervalMs) == WAIT_OBJECT_0)
                break;
        }

        LOG_MSG("Fps", "覆写线程退出");
        return 0;
    }

    static bool EnsureWriterThread()
    {
        if (g_thread)
            return true;

        if (!g_fpsAddr) {
            LOG_MSG("Fps", "帧率全局地址未解析到，无法开始覆写");
            return false;
        }

        g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_stop) {
            LOG("Fps", "CreateEvent 失败（错误 %lu）", GetLastError());
            return false;
        }

        g_thread = CreateThread(nullptr, 0, WriterProc, nullptr, 0, nullptr);
        if (!g_thread) {
            LOG("Fps", "CreateThread 失败（错误 %lu）", GetLastError());
            CloseHandle(g_stop);
            g_stop = nullptr;
            return false;
        }
        return true;
    }

    void Apply()
    {
        const Config::Values cfg = Config::Snapshot();

        g_target.store(NormalizeTarget(cfg.targetFps), std::memory_order_relaxed);
        g_enabled.store(cfg.fpsEnabled, std::memory_order_relaxed);
        g_clampEnabled.store(cfg.fpsGetterClamp, std::memory_order_relaxed);
        g_backgroundLimit.store(cfg.fpsBackgroundLimit, std::memory_order_relaxed);

        if (!cfg.fpsEnabled) {
            LOG_MSG("Fps", "配置里已关闭：停止周期性覆写");
            return;
        }

        if (!EnsureWriterThread())
            return;

        WriteOnce();

        LOG("Fps", "已应用：目标 %d fps，覆写间隔 %lu ms，getter 钳制=%d（上限 %d），后台限帧=%d（%d fps）",
            g_target.load(std::memory_order_relaxed), kWriteIntervalMs,
            cfg.fpsGetterClamp ? 1 : 0, kGetterClampMax,
            cfg.fpsBackgroundLimit ? 1 : 0, kBackgroundFps);
    }

    bool Init()
    {

        if (!ResolveTarget())
            return false;

        Apply();
        return true;
    }

    void Uninit()
    {
        // 先停钳制，避免卸载过程中 getter 还往 detour 里进。
        g_clampEnabled.store(false, std::memory_order_relaxed);
        g_getterHook.Detach();

        if (g_stop) {
            SetEvent(g_stop);
            if (g_thread) {
                if (WaitForSingleObject(g_thread, kThreadStopWaitMs) != WAIT_OBJECT_0)
                    LOG("Fps", "覆写线程未在 %lu ms 内退出", kThreadStopWaitMs);
                CloseHandle(g_thread);
                g_thread = nullptr;
            }
            CloseHandle(g_stop);
            g_stop = nullptr;
        }

    }
}

extern "C" __declspec(dllexport) int SetFps(int fps)
{
    FpsUnlock::g_target.store(FpsUnlock::NormalizeTarget(fps), std::memory_order_relaxed);
    FpsUnlock::WriteOnce();
    return FpsUnlock::g_fpsAddr ? 1 : 0;
}

extern "C" __declspec(dllexport) int GetFps(void)
{
    return FpsUnlock::g_target.load(std::memory_order_relaxed);
}
