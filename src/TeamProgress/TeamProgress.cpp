#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "TeamProgress.h"
#include "Config.h"
#include "Game.h"
#include "Logger.h"
#include "Patterns.h"
#include "Scanner.h"
#include <atomic>

namespace TeamProgress
{
    static Hooks::Hook<FnOpenTeam> g_openTeamHook;

    static FnCheckCanEnter           g_checkCanEnter           = nullptr;
    static FnOpenTeamPageAccordingly g_openTeamPageAccordingly = nullptr;

    static std::atomic<bool> g_active{ false };


    static uintptr_t ResolveFunction(const char* signature, uintptr_t fallbackRva,
                                     const char* name)
    {
        if (signature && signature[0]) {
            const uintptr_t hit = Scanner::ScanMainMod(signature);
            if (hit)
                return hit;
            LOG("Team", "%s：特征码未命中，改用 RVA 兜底", name);
        }

        const uintptr_t addr = Game::Resolve(fallbackRva);
        if (!addr)
            LOG("Team", "%s：RVA 0x%llX 也解析不到，该调用将不可用", name,
                static_cast<unsigned long long>(fallbackRva));
        return addr;
    }

    Hooks::Hook<FnOpenTeam>& OpenTeamHook() { return g_openTeamHook; }

    void __fastcall DetourOpenTeam()
    {
        HOOK_INFLIGHT_SCOPE();

        if (g_active.load(std::memory_order_relaxed) && g_checkCanEnter &&
            g_openTeamPageAccordingly) {

            if (g_checkCanEnter()) {
                g_openTeamPageAccordingly(0);
                return;
            }
        }


        if (FnOpenTeam original = g_openTeamHook.Original())
            original();
    }

    void Apply()
    {
        const Config::Values cfg = Config::Snapshot();
        g_active.store(cfg.teamProgressEnabled, std::memory_order_relaxed);

        LOG("Team", "已应用：跳过读条=%d", cfg.teamProgressEnabled ? 1 : 0);
    }

    bool Init()
    {
        if (!g_openTeamHook.Installed()) {
            LOG_MSG("Team", "OpenTeam 未 hook 上，功能不可用");
            return false;
        }

        g_checkCanEnter = reinterpret_cast<FnCheckCanEnter>(
            ResolveFunction(Patterns::Sig::CheckCanEnter,
                            Patterns::Rva::CheckCanEnter,
                            "CheckCanEnter"));

        g_openTeamPageAccordingly = reinterpret_cast<FnOpenTeamPageAccordingly>(
            ResolveFunction(Patterns::Sig::OpenTeamPageAccordingly,
                            Patterns::Rva::OpenTeamPageAccordingly,
                            "OpenTeamPageAccordingly"));

        if (!g_checkCanEnter) {
            LOG_MSG("Team", "取不到 CheckCanEnter，功能保持关闭");
            g_active.store(false, std::memory_order_relaxed);
            return false;
        }

        if (!g_openTeamPageAccordingly) {
            LOG_MSG("Team", "取不到 OpenTeamPageAccordingly，功能保持关闭");
            g_active.store(false, std::memory_order_relaxed);
            return false;
        }

        Apply();
        return true;
    }

    void Uninit()
    {
        g_active.store(false, std::memory_order_relaxed);
        g_openTeamHook.Detach();

        g_checkCanEnter           = nullptr;
        g_openTeamPageAccordingly = nullptr;
    }
}
