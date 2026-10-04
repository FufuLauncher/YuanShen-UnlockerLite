#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include "FovUnlock.h"
#include "FpsUnlock.h"
#include "Game.h"
#include "Patterns.h"
#include "SpawnWatch.h"
#include "TeamProgress.h"
#include <MinHook.h>
#include <atomic>

namespace Hooks
{
    namespace Detail
    {
        static SRWLOCK g_initLock = SRWLOCK_INIT;
        static bool    g_initialized = false;

        bool EnsureMinHook()
        {
            AcquireSRWLockExclusive(&g_initLock);

            if (!g_initialized) {
                const MH_STATUS st = MH_Initialize();
                g_initialized = (st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED);
                if (!g_initialized)
                    LOG("Hooks", "MH_Initialize 失败：%s", MH_StatusToString(st));
            }

            const bool ok = g_initialized;
            ReleaseSRWLockExclusive(&g_initLock);
            return ok;
        }
    }

    namespace InFlight
    {

        static std::atomic<long> g_count{ 0 };

        void Enter() noexcept { g_count.fetch_add(1, std::memory_order_seq_cst); }
        void Leave() noexcept { g_count.fetch_sub(1, std::memory_order_seq_cst); }
        long Count() noexcept { return g_count.load(std::memory_order_seq_cst); }

        bool WaitEmpty(DWORD timeoutMs)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMs;

            while (g_count.load(std::memory_order_seq_cst) != 0) {
                if (GetTickCount64() >= deadline)
                    return false;
                Sleep(1);
            }
            return true;
        }
    }

    bool EnableAll()
    {
        const MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
        if (st != MH_OK)
            LOG("Hooks", "EnableAll 失败：%s", MH_StatusToString(st));
        return st == MH_OK;
    }

    bool DisableAll()
    {
        const MH_STATUS st = MH_DisableHook(MH_ALL_HOOKS);
        if (st != MH_OK)
            LOG("Hooks", "DisableAll 失败：%s", MH_StatusToString(st));
        return st == MH_OK;
    }

    void RemoveAll()
    {
        const MH_STATUS st = MH_RemoveHook(MH_ALL_HOOKS);
        if (st != MH_OK)
            LOG("Hooks", "RemoveAll 失败：%s", MH_StatusToString(st));
    }

    bool ApplyQueued()
    {
        const MH_STATUS st = MH_ApplyQueued();
        if (st != MH_OK) {
            LOG("Hooks", "ApplyQueued 失败：%s（批次可能只生效了一部分，需整体回滚）",
                MH_StatusToString(st));
            return false;
        }
        LOG("Hooks", "已批量启用排队的 hook");
        return true;
    }

    bool Init()
    {
        if (!Detail::EnsureMinHook())
            return false;

        LOG("Hooks", "hook 子系统就绪（MinHook，主模块基址 %p）",
            reinterpret_cast<void*>(Game::ModuleBase()));

        Install(FovUnlock::SetFieldOfViewHook(),
                &FovUnlock::DetourSetFieldOfView,
                "Camera.set_fieldOfView",
                Patterns::Sig::CameraSetFieldOfView, 0);


        InstallCallTarget(FpsUnlock::GetTargetFrameRateHook(),
                          &FpsUnlock::DetourGetTargetFrameRate,
                          "Application.get_targetFrameRate",
                          Patterns::Sig::TargetFrameRateGetterCall,
                          Patterns::Rva::TargetFrameRateGetter);

        if (Install(TeamProgress::OpenTeamHook(),
                    &TeamProgress::DetourOpenTeam,
                    "OpenTeam",
                    Patterns::Sig::OpenTeam, 0)) {
            TeamProgress::Init();
        }

        // 给游戏内网页的浏览器进程注入兼容层
        InstallExport(SpawnWatch::CreateProcessWHook(),
                      &SpawnWatch::DetourCreateProcessW,
                      "CreateProcessW",
                      L"kernel32.dll", "CreateProcessW");

        return true;
    }

    void Uninit()
    {
        TeamProgress::Uninit();

        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(MH_ALL_HOOKS);

        if (Detail::g_initialized) {
            const MH_STATUS st = MH_Uninitialize();
            Detail::g_initialized = false;
            if (st != MH_OK)
                LOG("Hooks", "MH_Uninitialize 失败：%s", MH_StatusToString(st));
        }

        LOG_MSG("Hooks", "全部 hook 已卸载，MinHook 已反初始化");
    }
}
