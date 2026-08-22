#include "NvmlMonitor.h"
#include <windows.h>
#include <cstring>

typedef int nvmlReturn_t;
typedef void* nvmlDevice_t;

static constexpr nvmlReturn_t NVML_SUCCESS = 0;
static constexpr unsigned int NVML_TEMPERATURE_GPU = 0;

struct nvmlUtilization_t
{
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t
{
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

using nvmlInit_v2_t = nvmlReturn_t (*)();
using nvmlShutdown_t = nvmlReturn_t (*)();
using nvmlDeviceGetCount_v2_t = nvmlReturn_t (*)(unsigned int*);
using nvmlDeviceGetHandleByIndex_v2_t = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
using nvmlDeviceGetName_t = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
using nvmlDeviceGetUtilizationRates_t = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
using nvmlDeviceGetMemoryInfo_t = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
using nvmlDeviceGetTemperature_t = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);

static nvmlInit_v2_t p_nvmlInit_v2 = nullptr;
static nvmlShutdown_t p_nvmlShutdown = nullptr;
static nvmlDeviceGetCount_v2_t p_nvmlDeviceGetCount_v2 = nullptr;
static nvmlDeviceGetHandleByIndex_v2_t p_nvmlDeviceGetHandleByIndex_v2 = nullptr;
static nvmlDeviceGetName_t p_nvmlDeviceGetName = nullptr;
static nvmlDeviceGetUtilizationRates_t p_nvmlDeviceGetUtilizationRates = nullptr;
static nvmlDeviceGetMemoryInfo_t p_nvmlDeviceGetMemoryInfo = nullptr;
static nvmlDeviceGetTemperature_t p_nvmlDeviceGetTemperature = nullptr;

template <typename T>
bool loadProc(HMODULE dll, const char* name, T& target)
{
    target = reinterpret_cast<T>(GetProcAddress(dll, name));
    return target != nullptr;
}

NvmlMonitor::NvmlMonitor() = default;

NvmlMonitor::~NvmlMonitor()
{
    if (m_initialized && p_nvmlShutdown)
        p_nvmlShutdown();

    if (m_nvmlDll)
        FreeLibrary(static_cast<HMODULE>(m_nvmlDll));
}

bool NvmlMonitor::initialize()
{
    if (m_initialized)
        return true;

    HMODULE dll = LoadLibraryW(L"nvml.dll");
    if (!dll)
    {
        // NVIDIA drivers normally expose nvml.dll through the driver installation.
        // Try the common System32 location explicitly.
        dll = LoadLibraryW(L"C:\\Windows\\System32\\nvml.dll");
    }

    if (!dll)
        return false;

    m_nvmlDll = dll;

    bool ok =
        loadProc(dll, "nvmlInit_v2", p_nvmlInit_v2) &&
        loadProc(dll, "nvmlShutdown", p_nvmlShutdown) &&
        loadProc(dll, "nvmlDeviceGetCount_v2", p_nvmlDeviceGetCount_v2) &&
        loadProc(dll, "nvmlDeviceGetHandleByIndex_v2", p_nvmlDeviceGetHandleByIndex_v2) &&
        loadProc(dll, "nvmlDeviceGetName", p_nvmlDeviceGetName) &&
        loadProc(dll, "nvmlDeviceGetUtilizationRates", p_nvmlDeviceGetUtilizationRates) &&
        loadProc(dll, "nvmlDeviceGetMemoryInfo", p_nvmlDeviceGetMemoryInfo) &&
        loadProc(dll, "nvmlDeviceGetTemperature", p_nvmlDeviceGetTemperature);

    if (!ok || p_nvmlInit_v2() != NVML_SUCCESS)
        return false;

    unsigned int count = 0;
    if (p_nvmlDeviceGetCount_v2(&count) != NVML_SUCCESS || count == 0)
        return false;

    if (p_nvmlDeviceGetHandleByIndex_v2(0, reinterpret_cast<nvmlDevice_t*>(&m_device)) != NVML_SUCCESS)
        return false;

    m_initialized = true;
    return true;
}

GpuStats NvmlMonitor::query()
{
    GpuStats s;

    if (!m_initialized || !m_device)
        return s;

    s.available = true;

    char name[128] = {};
    if (p_nvmlDeviceGetName(reinterpret_cast<nvmlDevice_t>(m_device), name, sizeof(name)) == NVML_SUCCESS)
        s.name = name;

    nvmlUtilization_t util{};
    if (p_nvmlDeviceGetUtilizationRates(reinterpret_cast<nvmlDevice_t>(m_device), &util) == NVML_SUCCESS)
        s.utilization = util.gpu;

    nvmlMemory_t mem{};
    if (p_nvmlDeviceGetMemoryInfo(reinterpret_cast<nvmlDevice_t>(m_device), &mem) == NVML_SUCCESS)
    {
        s.memoryUsedBytes = mem.used;
        s.memoryTotalBytes = mem.total;
    }

    unsigned int temp = 0;
    if (p_nvmlDeviceGetTemperature(reinterpret_cast<nvmlDevice_t>(m_device),
                                    NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS)
        s.temperatureC = temp;

    return s;
}
