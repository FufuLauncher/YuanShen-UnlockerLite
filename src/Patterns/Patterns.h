#pragma once
#include <cstdint>

namespace Patterns
{
    namespace Rva
    {
        inline constexpr uintptr_t FpsLimit = 0x54CD60C;

       
        inline constexpr uintptr_t TargetFrameRateGetter = 0x145A4D0;

        inline constexpr uintptr_t CheckCanEnter           = 0xC8FE360;
        inline constexpr uintptr_t OpenTeamPageAccordingly = 0x8F3E550;
        inline constexpr uintptr_t OpenTeam                = 0x8F45C70;
    }

    namespace Sig
    {
        inline constexpr char FpsReadInFramePacer[] =
            "66 0F 6E 0D ?? ?? ?? ?? 0F 57 C0 0F 5B C9";

        inline constexpr char TargetFrameRateGetterCall[] =
            "E8 ? ? ? ? 85 C0 7E 0E E8 ? ? ? ? 0F 57 C0 F3 0F 2A C0 EB 08";

        inline constexpr char CameraSetFieldOfView[] =
            "40 53 48 83 EC 60 0F 29 74 24 ? 48 8B D9 0F 28 F1 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 48 8B C8";

        inline constexpr char CheckCanEnter[] =
            "56 48 81 EC 80 00 00 00 80 3D ?? ?? ?? ?? 00 0F 84 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00";

        inline constexpr char OpenTeamPageAccordingly[] =
            "56 57 53 48 83 EC 20 89 CB 80 3D ?? ?? ?? ?? 00 74 7A 80 3D ?? ?? ?? ?? 00 48 8B 05";

        inline constexpr char OpenTeam[] =
            "48 83 EC 28 80 3D ?? ?? ?? ?? 00 75 ?? 48 8B 0D ?? ?? ?? ?? 80 B9 C7 00 00 00 00 74 ?? B9 0C 00 00 00 E8 ?? ?? ?? ?? 84 C0 74";
    }
}
