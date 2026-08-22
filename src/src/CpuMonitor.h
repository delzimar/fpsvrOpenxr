#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <algorithm>

class CpuMonitor
{
public:
    CpuMonitor() = default;

    float query()
    {
        FILETIME idleTime{}, kernelTime{}, userTime{};
        if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
            return m_cachedUsage;

        auto toUint64 = [](const FILETIME& ft) -> uint64_t {
            return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };

        uint64_t idle = toUint64(idleTime);
        uint64_t kernel = toUint64(kernelTime);
        uint64_t user = toUint64(userTime);

        if (m_prevIdle == 0)
        {
            m_prevIdle = idle;
            m_prevKernel = kernel;
            m_prevUser = user;
            return 0.0f;
        }

        uint64_t idleDiff = idle - m_prevIdle;
        uint64_t kernelDiff = kernel - m_prevKernel;
        uint64_t userDiff = user - m_prevUser;
        uint64_t totalDiff = kernelDiff + userDiff;

        m_prevIdle = idle;
        m_prevKernel = kernel;
        m_prevUser = user;

        if (totalDiff > 0)
        {
            float usage = static_cast<float>(totalDiff - idleDiff) * 100.0f / static_cast<float>(totalDiff);
            m_cachedUsage = std::clamp(usage, 0.0f, 100.0f);
        }

        return m_cachedUsage;
    }

private:
    uint64_t m_prevIdle = 0;
    uint64_t m_prevKernel = 0;
    uint64_t m_prevUser = 0;
    float m_cachedUsage = 0.0f;
};
