#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Config.h"
#include "Ini.h"
#include "Logger.h"
#include "ProcessInfo.h"
#include <string>

namespace Config
{

    static SRWLOCK g_lock = SRWLOCK_INIT;
    static Values  g_values{};
    static wchar_t g_iniPath[MAX_PATH] = {};

    static void Publish(const Values& v)
    {
        AcquireSRWLockExclusive(&g_lock);
        g_values = v;
        ReleaseSRWLockExclusive(&g_lock);
    }

    static const wchar_t kIniFileName[] = L"Config.ini";

    static bool ResolveIniPath()
    {
        const wchar_t* dir = ProcessInfo::Get().self.dir;
        if (!dir[0])
            return false;
        return swprintf_s(g_iniPath, L"%s\\%s", dir, kIniFileName) > 0;
    }

    static bool ReadBool(const char* ini, const char* section, bool fallback)
    {
        char buf[64] = {};
        if (!Ini::GetValue(ini, section, "Value", buf, sizeof(buf)))
            return fallback;
        return Ini::ParseBool(buf, fallback);
    }

    static int ReadInt(const char* ini, const char* section, int fallback)
    {
        char buf[64] = {};
        if (!Ini::GetValue(ini, section, "Value", buf, sizeof(buf)))
            return fallback;
        return Ini::ParseInt(buf, fallback);
    }

    static void ReadInto(const char* ini, Values& v)
    {
        v.fpsEnabled        = ReadBool(ini, "Fps", v.fpsEnabled);
        v.targetFps         = ReadInt(ini, "TargetFps", v.targetFps);
        v.fpsGetterClamp    = ReadBool(ini, "FpsGetterClamp", v.fpsGetterClamp);
        v.fpsBackgroundLimit = ReadBool(ini, "FpsBackgroundLimit", v.fpsBackgroundLimit);
        v.fovEnabled        = ReadBool(ini, "Fov", v.fovEnabled);
        v.targetFov         = ReadInt(ini, "TargetFov", v.targetFov);
        v.fovRecoverEnabled = ReadBool(ini, "FovRecover", v.fovRecoverEnabled);
        v.teamProgressEnabled = ReadBool(ini, "TeamProgress", v.teamProgressEnabled);
        v.debugWaitAttach   = ReadBool(ini, "WaitDebugger", v.debugWaitAttach);

        LOG("Config", "生效配置：[Fps]=%d [TargetFps]=%d [FpsGetterClamp]=%d [FpsBackgroundLimit]=%d",
            v.fpsEnabled ? 1 : 0, v.targetFps, v.fpsGetterClamp ? 1 : 0,
            v.fpsBackgroundLimit ? 1 : 0);
        LOG("Config", "生效配置：[Fov]=%d [TargetFov]=%d [FovRecover]=%d [TeamProgress]=%d [WaitDebugger]=%d（后者仅 Debug 版有效）",
            v.fovEnabled ? 1 : 0, v.targetFov, v.fovRecoverEnabled ? 1 : 0,
            v.teamProgressEnabled ? 1 : 0, v.debugWaitAttach ? 1 : 0);
    }

    static bool LoadInto(Values& v)
    {
        if (!g_iniPath[0]) {
            LOG_MSG("Config", "ini 路径未知（ProcessInfo 未采集到本 DLL 位置），放弃读配置");
            return false;
        }

        const std::optional<std::string> content = Ini::Load(g_iniPath, 64 * 1024);
        if (!content) {
            LOG("Config", "读不到 %ls（错误 %lu），放弃读配置", g_iniPath, GetLastError());
            return false;
        }

        ReadInto(content->c_str(), v);
        return true;
    }

    void Init()
    {
        if (!ResolveIniPath())
            LOG_MSG("Config", "取不到本 DLL 路径，ini 路径未知");

        Values v{};                                  // 内置默认值打底
        LoadInto(v);                                 // 读不到就整份默认值
        Publish(v);

        LOG("Config", "ini 路径 %ls", g_iniPath);
    }

    void Reload()
    {
        Values v = Snapshot();
        if (LoadInto(v))
            Publish(v);

    }

    Values Snapshot()
    {
        Values v;
        AcquireSRWLockShared(&g_lock);
        v = g_values;
        ReleaseSRWLockShared(&g_lock);
        return v;
    }

    const wchar_t* IniPath() { return g_iniPath; }
}
