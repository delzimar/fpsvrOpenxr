#pragma once

#include <string>
#include <cstdint>

struct GpuStats
{
    bool available = false;
    std::string name;
    unsigned int utilization = 0;
    unsigned int temperatureC = 0;
    std::uint64_t memoryUsedBytes = 0;
    std::uint64_t memoryTotalBytes = 0;
};

class NvmlMonitor
{
public:
    NvmlMonitor();
    ~NvmlMonitor();

    bool initialize();
    GpuStats query();

private:
    void* m_nvmlDll = nullptr;
    void* m_device = nullptr;
    bool m_initialized = false;
};
