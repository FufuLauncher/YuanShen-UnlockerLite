#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Config.h"
#include "FovUnlock.h"
#include "FpsUnlock.h"
#include "Hooks.h"
#include "Logger.h"
#include "ProcessInfo.h"
#include "TeamProgress.h"
#include "Watcher.h"

namespace
{
    HMODULE g_hModule = nullptr;
    HANDLE  g_worker  = nullptr;
    HANDLE  g_stop    = nullptr;

    constexpr DWORD kWorkerStopWaitMs = 5000;
    constexpr DWORD kInflightDrainMs  = 5000;

#ifdef _DEBUG

    constexpr DWORD kDebuggerWaitMs = 60000;
#endif
	//热重载落地点，跑在 watcher 线程上：先重读（读不到就保持原值），再应用到各功能。
    //只做文件 IO + 原子量写入，watcher 线程上不能碰 loader。
    void OnConfigChanged()
    {
        Config::Reload();
        FpsUnlock::Apply();
        FovUnlock::Apply();
        TeamProgress::Apply();
    }

    void Shutdown()
    {
        LOG_MSG("Plugin", "开始卸载");

        Watcher::Stop();
        FpsUnlock::Uninit();
        FovUnlock::Uninit();
        TeamProgress::Uninit();

        Hooks::DisableAll();

        const long pending = Hooks::InFlight::Count();
        if (pending > 0)
            LOG("Plugin", "有 %ld 个在途调用，等待退出（最多 %lu ms）", pending, kInflightDrainMs);

        if (!Hooks::InFlight::WaitEmpty(kInflightDrainMs))
            LOG("Plugin", "在途调用未在 %lu ms 内归零（当前 %ld），继续卸载，有崩溃风险",
                kInflightDrainMs, Hooks::InFlight::Count());
        else
            LOG_MSG("Plugin", "在途调用已排空");

        Hooks::Uninit();
#ifdef _DEBUG
        Logger::CloseLog();
#endif
    }

    DWORD WINAPI WorkerProc(LPVOID)
    {

        ProcessInfo::Capture(g_hModule);

#ifdef _DEBUG
        Logger::InitLogFile();
#endif
        LOG_MSG("Plugin", "worker 启动");

        Config::Init();

#ifdef _DEBUG

        if (Config::Snapshot().debugWaitAttach && !IsDebuggerPresent()) {
            LOG("Plugin", "等待附加到进程（最多 %lu ms）...", kDebuggerWaitMs);
            const ULONGLONG deadline = GetTickCount64() + kDebuggerWaitMs;
            while (!IsDebuggerPresent() && GetTickCount64() < deadline)
                Sleep(50);

            if (IsDebuggerPresent()) {
                LOG_MSG("Plugin", "调试器已附加");
                DebugBreak();
            }
            else {
                LOG_MSG("Plugin", "等待调试器超时，继续初始化");
            }
        }
#endif

        ProcessInfo::Log();

        // 逐个初始化，不用短路写法 —— 保证每个模块都被尝试一次，
        // 失败原因全部落进日志，而不是被前一个失败盖住。
        bool ok = Hooks::Init();
        if (!FpsUnlock::Init())
            ok = false;
        if (!FovUnlock::Init())
            ok = false;

        if (ok)
            LOG_MSG("Plugin", "插件就绪");
        else
            LOG_MSG("Plugin", "初始化部分失败，详见上面的日志");

        // 热重载最后起：它一起来就可能立刻回调，必须等所有模块都初始化完。
        if (!Watcher::Start(Config::IniPath(), &OnConfigChanged))
            LOG_MSG("Plugin", "热重载未启动，改 Config.ini 需要重新注入才生效");

        // 挂起等退出信号；信号由 DllMain 的 DLL_PROCESS_DETACH 发出。
        WaitForSingleObject(g_stop, INFINITE);
        Shutdown();
        return 0;
    }
}

//主线程
#pragma region 主线程

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD   ul_reason_for_call,
	LPVOID  lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		g_hModule = hModule;

		g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!g_stop)
			return FALSE;

		g_worker = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
		if (!g_worker) {
			CloseHandle(g_stop);
			g_stop = nullptr;
			return FALSE;
		}
		break;

	case DLL_PROCESS_DETACH:
		if (!g_stop)
			break;

		SetEvent(g_stop);

		if (lpReserved == nullptr) {
			if (g_worker) {
				if (WaitForSingleObject(g_worker, kWorkerStopWaitMs) != WAIT_OBJECT_0)
					OutputDebugStringW(L"[YuanShen-UnlockerLite] worker 未在超时内退出\n");
				CloseHandle(g_worker);
				g_worker = nullptr;
			}
			CloseHandle(g_stop);
			g_stop = nullptr;
		}
		break;
	}
	return TRUE;
}

#pragma endregion
