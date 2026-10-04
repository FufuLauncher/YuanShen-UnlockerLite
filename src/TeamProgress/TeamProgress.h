#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"



namespace TeamProgress
{
    
    using FnOpenTeam = void(__fastcall*)();

    // void OpenTeamPageAccordingly(int mode)
    using FnOpenTeamPageAccordingly = void(__fastcall*)(int mode);

    // bool CheckCanEnter()
    using FnCheckCanEnter = bool(__fastcall*)();

    Hooks::Hook<FnOpenTeam>& OpenTeamHook();

    void __fastcall DetourOpenTeam();

    bool Init();

    void Apply();

    void Uninit();
}
