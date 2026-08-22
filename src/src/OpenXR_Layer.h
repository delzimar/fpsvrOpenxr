#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>
#include <deque>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif

#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif

#include "include/openxr/openxr.h"
#include "include/openxr/openxr_platform.h"
#include "include/openxr/openxr_loader_negotiation.h"
#include "NvmlMonitor.h"
#include "CpuMonitor.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"

// Nome oficial da API Layer
constexpr const char* OVERLAY_API_LAYER_NAME = "XR_APILAYER_NOVENDOR_performance_overlay";
constexpr uint32_t OVERLAY_API_LAYER_VERSION = 1;

// Funcao de log de debug formatado em Portugues
void LogDebug(const char* format, ...);

// Estrutura para estatísticas de memória RAM
struct RamStats {
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    float usagePercent = 0.0f;
};

// Tabela de despacho (Dispatch Table)
struct InstanceDispatchTable {
    PFN_xrGetInstanceProcAddr           pfnNextGetInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance               pfnDestroyInstance = nullptr;
    PFN_xrCreateSession                 pfnCreateSession = nullptr;
    PFN_xrDestroySession                pfnDestroySession = nullptr;
    PFN_xrCreateReferenceSpace          pfnCreateReferenceSpace = nullptr;
    PFN_xrDestroySpace                  pfnDestroySpace = nullptr;
    PFN_xrCreateSwapchain               pfnCreateSwapchain = nullptr;
    PFN_xrDestroySwapchain              pfnDestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages      pfnEnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage         pfnAcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage            pfnWaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage         pfnReleaseSwapchainImage = nullptr;
    PFN_xrBeginFrame                    pfnBeginFrame = nullptr;
    PFN_xrEndFrame                      pfnEndFrame = nullptr;
};

// Contexto de sessao do Overlay
struct OverlaySessionContext {
    XrInstance                          instance = XR_NULL_HANDLE;
    XrSession                           session = XR_NULL_HANDLE;
    InstanceDispatchTable*              dispatch = nullptr;

    // Recursos DirectX 11
    Microsoft::WRL::ComPtr<ID3D11Device>        d3d11Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11Context;

    // Recursos OpenXR para o Overlay (Espaço fixado no mundo 3D / Cockpit)
    XrSpace                             overlaySpace = XR_NULL_HANDLE;
    XrSwapchain                         overlaySwapchain = XR_NULL_HANDLE;
    uint32_t                            overlayWidth = 512;
    uint32_t                            overlayHeight = 256;
    std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> rtvList;

    // Estado do Dear ImGui
    ImGuiContext*                       imguiContext = nullptr;
    bool                                imguiInitialized = false;

    // Controle de Visibilidade, Escala e Movimentação 3D em Tempo Real (Atalhos)
    bool                                overlayVisible = true;
    bool                                hotkeyWasPressed = false;
    float                               overlayScale = 1.0f;
    bool                                plusWasPressed = false;
    bool                                minusWasPressed = false;
    float                               overlayOffsetX = 0.0f;
    float                               overlayOffsetY = 0.0f;
    bool                                upWasPressed = false;
    bool                                downWasPressed = false;
    bool                                leftWasPressed = false;
    bool                                rightWasPressed = false;

    // Monitoramento NVML (GPU / VGA)
    NvmlMonitor                         nvml;
    GpuStats                            cachedGpuStats;
    std::chrono::steady_clock::time_point lastNvmlQueryTime{};
    float                               displayedGpuFrameTimeMs = 0.0f;
    float                               gpuAccumulatedTimeMs = 0.0f;

    // Monitoramento CPU & RAM
    CpuMonitor                          cpu;
    float                               cachedCpuUsage = 0.0f;
    RamStats                            cachedRamStats{};
    std::chrono::steady_clock::time_point lastSysQueryTime{};
    std::chrono::steady_clock::time_point cpuBeginTime{};
    float                               currentCpuFrameTimeMs = 0.0f;
    float                               displayedCpuFrameTimeMs = 0.0f;
    float                               cpuAccumulatedTimeMs = 0.0f;

    // Métricas de FPS, FPS Médio, 1% Low e Frame Time Estabilizados
    std::chrono::steady_clock::time_point lastFrameTime{};
    float                               displayedFps = 0.0f;
    float                               displayedAvgFps = 0.0f;
    float                               displayed1PercentLowFps = 0.0f;
    float                               displayedFrameTimeMs = 0.0f;
    float                               fpsAccumulatedTimeMs = 0.0f;
    uint32_t                            fpsAccumulatedFrames = 0;
    float                               frameTimeHistory[60] = { 0 };
    int                                 historyIndex = 0;

    // Buffer de amostras recentes para cálculo de 1% Low e Média
    std::deque<float>                   frametimeWindow;
    uint64_t                            totalSessionFrames = 0;
    double                              totalSessionTimeMs = 0.0;
};

// Declaracao das funcoes de interceptacao do OpenXR
extern "C" {

XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* apiLayerRequest);

}
