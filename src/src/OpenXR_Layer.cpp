#include "OpenXR_Layer.h"
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <vector>

// Armazenamento global thread-safe para instâncias e sessões ativas
static std::mutex g_layerMutex;
static std::unordered_map<XrInstance, InstanceDispatchTable> g_instanceDispatchTables;
static std::unordered_map<XrSession, std::unique_ptr<OverlaySessionContext>> g_sessionContexts;

// Caminho do arquivo de log persistente para diagnóstico instantâneo
static const char* LOG_FILE_PATH = "C:\\Users\\DVGA\\AppData\\Local\\Temp\\OpenXR_Performance_Overlay.log";

// =========================================================================================
// SISTEMA DE LOG DE DEBUG EM PORTUGUÊS (OutputDebugStringA + Arquivo de Log)
// =========================================================================================
void LogDebug(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written > 0)
    {
        char formattedBuffer[1150];
        snprintf(formattedBuffer, sizeof(formattedBuffer), "[OpenXR_Performance_Overlay] %s\n", buffer);
        
        OutputDebugStringA(formattedBuffer);

        static std::mutex fileMutex;
        std::lock_guard<std::mutex> fileLock(fileMutex);
        FILE* f = nullptr;
        if (fopen_s(&f, LOG_FILE_PATH, "a") == 0 && f)
        {
            fprintf(f, "%s", formattedBuffer);
            fclose(f);
        }
    }
}

// =========================================================================================
// FUNÇÕES AUXILIARES DE MONITORAMENTO E CORES
// =========================================================================================
static RamStats QueryRamStats()
{
    RamStats s{};
    MEMORYSTATUSEX memInfo{};
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo))
    {
        s.totalBytes = memInfo.ullTotalPhys;
        s.usedBytes = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
        s.usagePercent = static_cast<float>(memInfo.dwMemoryLoad);
    }
    return s;
}

// Cor progressiva dinâmica: Verde (<60%) -> Amarelo (60-80%) -> Laranja (80-90%) -> Vermelho Alerta (>90%)
static ImVec4 GetProgressColor(float ratio)
{
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    if (ratio < 0.60f)
    {
        return ImVec4(0.20f, 0.90f, 0.35f, 1.0f); // Verde brilhante
    }
    else if (ratio < 0.80f)
    {
        float t = (ratio - 0.60f) / 0.20f;
        return ImVec4(0.20f + 0.75f * t, 0.90f - 0.05f * t, 0.35f * (1.0f - t), 1.0f); // Verde -> Amarelo
    }
    else if (ratio < 0.90f)
    {
        float t = (ratio - 0.80f) / 0.10f;
        return ImVec4(0.95f + 0.05f * t, 0.85f - 0.35f * t, 0.0f, 1.0f); // Amarelo -> Laranja
    }
    else
    {
        return ImVec4(1.0f, 0.20f, 0.20f, 1.0f); // Vermelho Alerta / Limite
    }
}

// =========================================================================================
// DECLARAÇÕES ANTECIPADAS DAS FUNÇÕES INTERCEPTADAS
// =========================================================================================
static XrResult XRAPI_PTR Layer_xrGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function);

static XrResult XRAPI_PTR Layer_xrCreateApiLayerInstance(
    const XrInstanceCreateInfo* info,
    const struct XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance);

static XrResult XRAPI_PTR Layer_xrDestroyInstance(
    XrInstance instance);

static XrResult XRAPI_PTR Layer_xrCreateSession(
    XrInstance instance,
    const XrSessionCreateInfo* createInfo,
    XrSession* session);

static XrResult XRAPI_PTR Layer_xrDestroySession(
    XrSession session);

static XrResult XRAPI_PTR Layer_xrBeginFrame(
    XrSession session,
    const XrFrameBeginInfo* frameBeginInfo);

static XrResult XRAPI_PTR Layer_xrEndFrame(
    XrSession session,
    const XrFrameEndInfo* frameEndInfo);

// =========================================================================================
// 1. NEGOCIAÇÃO COM O OPENXR LOADER
// =========================================================================================
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* apiLayerRequest)
{
    LogDebug("xrNegotiateLoaderApiLayerInterface chamado! Layer: %s", layerName ? layerName : "<nulo>");

    if (!loaderInfo || !layerName || !apiLayerRequest)
    {
        LogDebug("ERRO: Parametros nulos fornecidos na negociacao.");
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO)
    {
        LogDebug("ERRO: loaderInfo->structType invalido (%d)", loaderInfo->structType);
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    if (strcmp(layerName, OVERLAY_API_LAYER_NAME) != 0)
    {
        LogDebug("AVISO: Nome de camada '%s' != '%s'", layerName, OVERLAY_API_LAYER_NAME);
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
    {
        LogDebug("ERRO: Versao de interface incompativel (min: %u, max: %u, layer: %u)",
                 loaderInfo->minInterfaceVersion, loaderInfo->maxInterfaceVersion, XR_CURRENT_LOADER_API_LAYER_VERSION);
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    apiLayerRequest->structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
    apiLayerRequest->structVersion = XR_API_LAYER_INFO_STRUCT_VERSION;
    apiLayerRequest->structSize = sizeof(XrNegotiateApiLayerRequest);
    apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
    apiLayerRequest->getInstanceProcAddr = Layer_xrGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = Layer_xrCreateApiLayerInstance;

    LogDebug("Negociacao com OpenXR Loader concluida com SUCESSO!");
    return XR_SUCCESS;
}

// =========================================================================================
// 2. CRIAÇÃO DE INSTÂNCIA DA API LAYER (xrCreateApiLayerInstance)
// =========================================================================================
static XrResult XRAPI_PTR Layer_xrCreateApiLayerInstance(
    const XrInstanceCreateInfo* info,
    const struct XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance)
{
    const char* appName = (info && info->applicationInfo.applicationName[0]) ? info->applicationInfo.applicationName : "<desconhecido>";
    LogDebug("Layer_xrCreateApiLayerInstance chamado pela aplicacao: '%s' (OpenXR Engine: '%s')",
             appName, info ? info->applicationInfo.engineName : "<desconhecido>");

    if (!apiLayerInfo || !apiLayerInfo->nextInfo || !instance)
    {
        LogDebug("ERRO: apiLayerInfo ou nextInfo e nulo ao criar instancia.");
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    PFN_xrGetInstanceProcAddr nextGetInstanceProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;
    PFN_xrCreateApiLayerInstance nextCreateApiLayerInstance = apiLayerInfo->nextInfo->nextCreateApiLayerInstance;

    if (!nextGetInstanceProcAddr || !nextCreateApiLayerInstance)
    {
        LogDebug("ERRO: Ponteiros downstream nulos em apiLayerInfo->nextInfo.");
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    // Prepara o encadeamento avançando a lista ligada nextInfo
    XrApiLayerCreateInfo chainApiLayerInfo = *apiLayerInfo;
    chainApiLayerInfo.nextInfo = apiLayerInfo->nextInfo->next;

    // Chama o próximo da cadeia
    XrResult result = nextCreateApiLayerInstance(info, &chainApiLayerInfo, instance);
    if (XR_FAILED(result))
    {
        LogDebug("ERRO: Falha ao criar instancia downstream: codigo %d", result);
        return result;
    }

    // Constrói a tabela de despacho downstream
    InstanceDispatchTable dispatchTable{};
    dispatchTable.pfnNextGetInstanceProcAddr = nextGetInstanceProcAddr;

    #define LOAD_DOWNSTREAM_PROC(fnName, target) \
        nextGetInstanceProcAddr(*instance, #fnName, reinterpret_cast<PFN_xrVoidFunction*>(&dispatchTable.target));

    LOAD_DOWNSTREAM_PROC(xrDestroyInstance, pfnDestroyInstance);
    LOAD_DOWNSTREAM_PROC(xrCreateSession, pfnCreateSession);
    LOAD_DOWNSTREAM_PROC(xrDestroySession, pfnDestroySession);
    LOAD_DOWNSTREAM_PROC(xrCreateReferenceSpace, pfnCreateReferenceSpace);
    LOAD_DOWNSTREAM_PROC(xrDestroySpace, pfnDestroySpace);
    LOAD_DOWNSTREAM_PROC(xrCreateSwapchain, pfnCreateSwapchain);
    LOAD_DOWNSTREAM_PROC(xrDestroySwapchain, pfnDestroySwapchain);
    LOAD_DOWNSTREAM_PROC(xrEnumerateSwapchainImages, pfnEnumerateSwapchainImages);
    LOAD_DOWNSTREAM_PROC(xrAcquireSwapchainImage, pfnAcquireSwapchainImage);
    LOAD_DOWNSTREAM_PROC(xrWaitSwapchainImage, pfnWaitSwapchainImage);
    LOAD_DOWNSTREAM_PROC(xrReleaseSwapchainImage, pfnReleaseSwapchainImage);
    LOAD_DOWNSTREAM_PROC(xrBeginFrame, pfnBeginFrame);
    LOAD_DOWNSTREAM_PROC(xrEndFrame, pfnEndFrame);

    #undef LOAD_DOWNSTREAM_PROC

    {
        std::lock_guard<std::mutex> lock(g_layerMutex);
        g_instanceDispatchTables[*instance] = dispatchTable;
    }

    LogDebug("Instancia OpenXR 0x%p registrada com SUCESSO na API Layer.", *instance);
    return XR_SUCCESS;
}

// =========================================================================================
// 3. TABELA DE DESPACHO E INTERCEPTAÇÃO (xrGetInstanceProcAddr)
// =========================================================================================
static XrResult XRAPI_PTR Layer_xrGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function)
{
    if (!name || !function)
        return XR_ERROR_VALIDATION_FAILURE;

    *function = nullptr;

    if (strcmp(name, "xrGetInstanceProcAddr") == 0)
    {
        *function = reinterpret_cast<PFN_xrVoidFunction>(Layer_xrGetInstanceProcAddr);
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrCreateApiLayerInstance") == 0)
    {
        *function = reinterpret_cast<PFN_xrVoidFunction>(Layer_xrCreateApiLayerInstance);
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrDestroyInstance") == 0)
    {
        *function = reinterpret_cast<PFN_xrVoidFunction>(Layer_xrDestroyInstance);
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrCreateSession") == 0)
    {
        *function = reinterpret_cast<PFN_xrVoidFunction>(Layer_xrCreateSession);
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrDestroySession") == 0)
    {
        *function = reinterpret_cast<PFN_xrVoidFunction>(Layer_xrDestroySession);
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrBeginFrame") == 0)
    {
        *function = reinterpret_cast<PFN_xrVoidFunction>(Layer_xrBeginFrame);
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrEndFrame") == 0)
    {
        *function = reinterpret_cast<PFN_xrVoidFunction>(Layer_xrEndFrame);
        return XR_SUCCESS;
    }

    // Repassa para a próxima camada/runtime através da dispatch table
    std::lock_guard<std::mutex> lock(g_layerMutex);
    auto it = g_instanceDispatchTables.find(instance);
    if (it != g_instanceDispatchTables.end() && it->second.pfnNextGetInstanceProcAddr)
    {
        return it->second.pfnNextGetInstanceProcAddr(instance, name, function);
    }

    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

// =========================================================================================
// 4. DESTRUIÇÃO DE INSTÂNCIA (xrDestroyInstance)
// =========================================================================================
static XrResult XRAPI_PTR Layer_xrDestroyInstance(XrInstance instance)
{
    LogDebug("Destruindo Instancia OpenXR: 0x%p", instance);

    PFN_xrDestroyInstance downstreamDestroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_layerMutex);
        auto it = g_instanceDispatchTables.find(instance);
        if (it != g_instanceDispatchTables.end())
        {
            downstreamDestroy = it->second.pfnDestroyInstance;
            g_instanceDispatchTables.erase(it);
        }
    }

    if (downstreamDestroy)
    {
        return downstreamDestroy(instance);
    }

    return XR_SUCCESS;
}

// =========================================================================================
// 5. INTERCEPTAÇÃO DE SESSÃO E INICIALIZAÇÃO DE RECURSOS D3D11 / IMGUI / SWAPCHAIN
// =========================================================================================
static XrResult XRAPI_PTR Layer_xrCreateSession(
    XrInstance instance,
    const XrSessionCreateInfo* createInfo,
    XrSession* session)
{
    LogDebug("Interceptando xrCreateSession...");

    if (!createInfo || !session)
        return XR_ERROR_VALIDATION_FAILURE;

    InstanceDispatchTable* dispatch = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_layerMutex);
        auto it = g_instanceDispatchTables.find(instance);
        if (it != g_instanceDispatchTables.end())
        {
            dispatch = &it->second;
        }
    }

    if (!dispatch || !dispatch->pfnCreateSession)
    {
        LogDebug("ERRO: Dispatch Table nao encontrada para instancia 0x%p", instance);
        return XR_ERROR_HANDLE_INVALID;
    }

    // 5.1 Busca pelo dispositivo DirectX 11
    ID3D11Device* appD3D11Device = nullptr;
    const XrBaseInStructure* nextHeader = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);
    while (nextHeader != nullptr)
    {
        if (nextHeader->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)
        {
            const auto* d3dBinding = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(nextHeader);
            appD3D11Device = d3dBinding->device;
            LogDebug("DirectX 11 Device detectado com sucesso na aplicacao: 0x%p", appD3D11Device);
            break;
        }
        nextHeader = nextHeader->next;
    }

    XrResult result = dispatch->pfnCreateSession(instance, createInfo, session);
    if (XR_FAILED(result) || *session == XR_NULL_HANDLE)
    {
        LogDebug("ERRO: Falha ao criar sessao no runtime downstream: codigo %d", result);
        return result;
    }

    if (!appD3D11Device)
    {
        LogDebug("AVISO: Aplicacao iniciou sessao OpenXR sem D3D11. Overlay D3D11 inativo.");
        return result;
    }

    // 5.2 Contexto do Overlay
    auto ctx = std::make_unique<OverlaySessionContext>();
    ctx->instance = instance;
    ctx->session = *session;
    ctx->dispatch = dispatch;
    ctx->d3d11Device = appD3D11Device;
    appD3D11Device->GetImmediateContext(ctx->d3d11Context.GetAddressOf());

    // 5.3 Criação do Espaço de Referência 3D LOCAL (Fixo no mundo/cockpit, igual fpsVR)
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    spaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

    XrResult spaceRes = dispatch->pfnCreateReferenceSpace(*session, &spaceInfo, &ctx->overlaySpace);
    if (XR_FAILED(spaceRes))
    {
        LogDebug("AVISO: Espaco LOCAL nao suportado, tentando fallback para VIEW...");
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        spaceRes = dispatch->pfnCreateReferenceSpace(*session, &spaceInfo, &ctx->overlaySpace);
    }

    if (XR_FAILED(spaceRes))
    {
        LogDebug("ERRO: Falha ao criar espaco para o overlay: codigo %d", spaceRes);
    }
    else
    {
        LogDebug("Espaco 3D (LOCAL/Fixo no Cockpit) criado com SUCESSO.");
    }

    // 5.4 Swapchain OpenXR
    XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainInfo.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = ctx->overlayWidth;
    swapchainInfo.height = ctx->overlayHeight;
    swapchainInfo.faceCount = 1;
    swapchainInfo.arraySize = 1;
    swapchainInfo.mipCount = 1;

    XrResult scRes = dispatch->pfnCreateSwapchain(*session, &swapchainInfo, &ctx->overlaySwapchain);
    if (XR_FAILED(scRes))
    {
        LogDebug("ERRO CRITICO: Falha ao criar XrSwapchain para o overlay: codigo %d", scRes);
    }
    else
    {
        LogDebug("XrSwapchain do Overlay criado com sucesso (%ux%u)", ctx->overlayWidth, ctx->overlayHeight);

        uint32_t imageCount = 0;
        dispatch->pfnEnumerateSwapchainImages(ctx->overlaySwapchain, 0, &imageCount, nullptr);

        std::vector<XrSwapchainImageD3D11KHR> d3dImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        dispatch->pfnEnumerateSwapchainImages(
            ctx->overlaySwapchain,
            imageCount,
            &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(d3dImages.data()));

        ctx->rtvList.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;

            HRESULT hr = ctx->d3d11Device->CreateRenderTargetView(
                d3dImages[i].texture,
                &rtvDesc,
                ctx->rtvList[i].GetAddressOf());

            if (FAILED(hr))
            {
                LogDebug("ERRO: Falha ao criar ID3D11RenderTargetView para imagem %u (HRESULT: 0x%08X)", i, hr);
            }
        }
    }

    // 5.5 Inicialização do Dear ImGui
    IMGUI_CHECKVERSION();
    ctx->imguiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx->imguiContext);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(ctx->overlayWidth), static_cast<float>(ctx->overlayHeight));

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.FrameRounding = 5.0f;
    style.ItemSpacing = ImVec2(5.0f, 3.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.05f, 0.08f, 0.92f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.70f, 1.0f, 0.85f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.20f, 0.90f, 0.40f, 1.0f);

    if (ImGui_ImplDX11_Init(ctx->d3d11Device.Get(), ctx->d3d11Context.Get()))
    {
        ctx->imguiInitialized = true;
        LogDebug("Dear ImGui com backend D3D11 inicializado com SUCESSO.");
    }

    // 5.6 Inicialização de Monitores de Hardware (NVML, CPU e RAM)
    if (ctx->nvml.initialize())
    {
        ctx->cachedGpuStats = ctx->nvml.query();
        ctx->lastNvmlQueryTime = std::chrono::steady_clock::now();
    }
    ctx->cachedCpuUsage = ctx->cpu.query();
    ctx->cachedRamStats = QueryRamStats();
    ctx->lastSysQueryTime = std::chrono::steady_clock::now();
    ctx->lastFrameTime = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(g_layerMutex);
        g_sessionContexts[*session] = std::move(ctx);
    }

    LogDebug("Sessao OpenXR 0x%p inicializada com SUCESSO!", *session);
    return XR_SUCCESS;
}

// =========================================================================================
// 6. DESTRUIÇÃO DE SESSÃO E LIBERAÇÃO DE RECURSOS
// =========================================================================================
static XrResult XRAPI_PTR Layer_xrDestroySession(XrSession session)
{
    LogDebug("Destruindo Sessao OpenXR: 0x%p", session);

    std::unique_ptr<OverlaySessionContext> ctx;
    {
        std::lock_guard<std::mutex> lock(g_layerMutex);
        auto it = g_sessionContexts.find(session);
        if (it != g_sessionContexts.end())
        {
            ctx = std::move(it->second);
            g_sessionContexts.erase(it);
        }
    }

    if (ctx)
    {
        if (ctx->imguiInitialized)
        {
            ImGui::SetCurrentContext(ctx->imguiContext);
            ImGui_ImplDX11_Shutdown();
            ImGui::DestroyContext(ctx->imguiContext);
            ctx->imguiInitialized = false;
        }

        if (ctx->overlaySwapchain != XR_NULL_HANDLE && ctx->dispatch && ctx->dispatch->pfnDestroySwapchain)
        {
            ctx->dispatch->pfnDestroySwapchain(ctx->overlaySwapchain);
            ctx->overlaySwapchain = XR_NULL_HANDLE;
        }

        if (ctx->overlaySpace != XR_NULL_HANDLE && ctx->dispatch && ctx->dispatch->pfnDestroySpace)
        {
            ctx->dispatch->pfnDestroySpace(ctx->overlaySpace);
            ctx->overlaySpace = XR_NULL_HANDLE;
        }

        ctx->rtvList.clear();
        ctx->d3d11Context.Reset();
        ctx->d3d11Device.Reset();

        if (ctx->dispatch && ctx->dispatch->pfnDestroySession)
        {
            return ctx->dispatch->pfnDestroySession(session);
        }
    }

    return XR_SUCCESS;
}

// =========================================================================================
// 7. INTERCEPTAÇÃO DO xrBeginFrame (Cálculo do CPU Frame Time)
// =========================================================================================
static XrResult XRAPI_PTR Layer_xrBeginFrame(
    XrSession session,
    const XrFrameBeginInfo* frameBeginInfo)
{
    OverlaySessionContext* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_layerMutex);
        auto it = g_sessionContexts.find(session);
        if (it != g_sessionContexts.end())
        {
            ctx = it->second.get();
        }
    }

    if (ctx)
    {
        ctx->cpuBeginTime = std::chrono::steady_clock::now();
        if (ctx->dispatch && ctx->dispatch->pfnBeginFrame)
        {
            return ctx->dispatch->pfnBeginFrame(session, frameBeginInfo);
        }
    }

    return XR_ERROR_HANDLE_INVALID;
}

// =========================================================================================
// 8. INTERCEPTAÇÃO DO xrEndFrame, RENDERIZAÇÃO E INJEÇÃO DO OVERLAY
// =========================================================================================
static XrResult XRAPI_PTR Layer_xrEndFrame(
    XrSession session,
    const XrFrameEndInfo* frameEndInfo)
{
    if (!frameEndInfo)
        return XR_ERROR_VALIDATION_FAILURE;

    OverlaySessionContext* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_layerMutex);
        auto it = g_sessionContexts.find(session);
        if (it != g_sessionContexts.end())
        {
            ctx = it->second.get();
        }
    }

    if (!ctx || !ctx->dispatch || !ctx->dispatch->pfnEndFrame)
    {
        return XR_ERROR_HANDLE_INVALID;
    }

    // -------------------------------------------------------------------------------------
    // 8.1 Verificação dos Atalhos de Teclado (Visibilidade, Escala e Posição em Tempo Real)
    // -------------------------------------------------------------------------------------
    bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool oPressed = (GetAsyncKeyState('O') & 0x8000) != 0;

    bool plusPressed = (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) != 0 || (GetAsyncKeyState(VK_ADD) & 0x8000) != 0;
    bool minusPressed = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0 || (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0;

    bool upPressed = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    bool downPressed = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    bool leftPressed = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    bool rightPressed = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

    // 1. Toggle de Visibilidade: Ctrl + Shift + O
    bool toggleTriggered = (ctrlPressed && shiftPressed && oPressed);
    if (toggleTriggered && !ctx->hotkeyWasPressed)
    {
        ctx->overlayVisible = !ctx->overlayVisible;
        LogDebug("Overlay alternado via atalho [Ctrl+Shift+O]! Estado: %s", ctx->overlayVisible ? "LIGADO" : "DESLIGADO");
    }
    ctx->hotkeyWasPressed = toggleTriggered;

    // 2. Aumentar Escala: Ctrl + Shift + (+)
    bool plusTriggered = (ctrlPressed && shiftPressed && plusPressed);
    if (plusTriggered && !ctx->plusWasPressed)
    {
        ctx->overlayScale = (std::min)(ctx->overlayScale + 0.05f, 2.50f);
        LogDebug("Overlay escala aumentada: %.0f %%", ctx->overlayScale * 100.0f);
    }
    ctx->plusWasPressed = plusTriggered;

    // 3. Diminuir Escala: Ctrl + Shift + (-)
    bool minusTriggered = (ctrlPressed && shiftPressed && minusPressed);
    if (minusTriggered && !ctx->minusWasPressed)
    {
        ctx->overlayScale = (std::max)(ctx->overlayScale - 0.05f, 0.40f);
        LogDebug("Overlay escala reduzida: %.0f %%", ctx->overlayScale * 100.0f);
    }
    ctx->minusWasPressed = minusTriggered;

    // 4. Movimentação 3D: Ctrl + Shift + Setas (Cima, Baixo, Esquerda, Direita)
    bool upTriggered = (ctrlPressed && shiftPressed && upPressed);
    if (upTriggered && !ctx->upWasPressed)
    {
        ctx->overlayOffsetY += 0.02f; // +2 cm para cima
        LogDebug("Overlay movido para CIMA: Y = %.2f m", -0.22f + ctx->overlayOffsetY);
    }
    ctx->upWasPressed = upTriggered;

    bool downTriggered = (ctrlPressed && shiftPressed && downPressed);
    if (downTriggered && !ctx->downWasPressed)
    {
        ctx->overlayOffsetY -= 0.02f; // -2 cm para baixo
        LogDebug("Overlay movido para BAIXO: Y = %.2f m", -0.22f + ctx->overlayOffsetY);
    }
    ctx->downWasPressed = downTriggered;

    bool leftTriggered = (ctrlPressed && shiftPressed && leftPressed);
    if (leftTriggered && !ctx->leftWasPressed)
    {
        ctx->overlayOffsetX -= 0.02f; // -2 cm para a esquerda
        LogDebug("Overlay movido para a ESQUERDA: X = %.2f m", ctx->overlayOffsetX);
    }
    ctx->leftWasPressed = leftTriggered;

    bool rightTriggered = (ctrlPressed && shiftPressed && rightPressed);
    if (rightTriggered && !ctx->rightWasPressed)
    {
        ctx->overlayOffsetX += 0.02f; // +2 cm para a direita
        LogDebug("Overlay movido para a DIREITA: X = %.2f m", ctx->overlayOffsetX);
    }
    ctx->rightWasPressed = rightTriggered;

    // Se o overlay estiver desligado, repassa diretamente com zero processamento
    if (!ctx->overlayVisible || ctx->overlaySwapchain == XR_NULL_HANDLE ||
        ctx->overlaySpace == XR_NULL_HANDLE || !ctx->imguiInitialized || ctx->rtvList.empty())
    {
        return ctx->dispatch->pfnEndFrame(session, frameEndInfo);
    }

    // -------------------------------------------------------------------------------------
    // 8.2 Cálculo de FPS, FPS Médio, 1% Low e Tempos Estabilizados
    // -------------------------------------------------------------------------------------
    auto now = std::chrono::steady_clock::now();
    float deltaMs = std::chrono::duration<float, std::milli>(now - ctx->lastFrameTime).count();
    ctx->lastFrameTime = now;

    // CPU Frame Time: Tempo que a CPU gastou processando e enviando comandos
    float cpuDeltaMs = 0.0f;
    if (ctx->cpuBeginTime.time_since_epoch().count() > 0)
    {
        cpuDeltaMs = std::chrono::duration<float, std::milli>(now - ctx->cpuBeginTime).count();
    }

    // GPU Frame Time: Tempo de execução da GPU estimado pelo tempo de quadro e carga
    float gpuDeltaMs = 0.0f;
    if (deltaMs > 0.0f && deltaMs < 500.0f && ctx->cachedGpuStats.available)
    {
        float gpuLoadRatio = std::clamp(static_cast<float>(ctx->cachedGpuStats.utilization) / 100.0f, 0.05f, 1.0f);
        gpuDeltaMs = deltaMs * gpuLoadRatio;
    }

    if (deltaMs > 0.0f && deltaMs < 500.0f)
    {
        ctx->frameTimeHistory[ctx->historyIndex] = deltaMs;
        ctx->historyIndex = (ctx->historyIndex + 1) % 60;

        ctx->fpsAccumulatedFrames++;
        ctx->fpsAccumulatedTimeMs += deltaMs;
        ctx->cpuAccumulatedTimeMs += cpuDeltaMs;
        ctx->gpuAccumulatedTimeMs += gpuDeltaMs;

        ctx->totalSessionFrames++;
        ctx->totalSessionTimeMs += deltaMs;

        // Amostras recentes para cálculo do 1% Low (janela móvel de até 500 frames)
        ctx->frametimeWindow.push_back(deltaMs);
        if (ctx->frametimeWindow.size() > 500)
        {
            ctx->frametimeWindow.pop_front();
        }

        // Atualização suave dos números a cada 400ms para leitura sólida
        if (ctx->fpsAccumulatedTimeMs >= 400.0f)
        {
            ctx->displayedFps = (ctx->fpsAccumulatedFrames * 1000.0f) / ctx->fpsAccumulatedTimeMs;
            ctx->displayedFrameTimeMs = ctx->fpsAccumulatedTimeMs / ctx->fpsAccumulatedFrames;
            ctx->displayedCpuFrameTimeMs = ctx->cpuAccumulatedTimeMs / ctx->fpsAccumulatedFrames;
            ctx->displayedGpuFrameTimeMs = ctx->gpuAccumulatedTimeMs / ctx->fpsAccumulatedFrames;

            // FPS Médio
            if (ctx->totalSessionTimeMs > 0.0)
            {
                ctx->displayedAvgFps = static_cast<float>((ctx->totalSessionFrames * 1000.0) / ctx->totalSessionTimeMs);
            }

            // 1% Low FPS (Média dos 1% piores frametimes)
            if (ctx->frametimeWindow.size() >= 20)
            {
                std::vector<float> sortedSamples(ctx->frametimeWindow.begin(), ctx->frametimeWindow.end());
                std::sort(sortedSamples.begin(), sortedSamples.end());

                size_t low1pCount = (std::max)(size_t(1), static_cast<size_t>(sortedSamples.size() * 0.01f));
                float worstFrametimesSum = 0.0f;
                for (size_t i = sortedSamples.size() - low1pCount; i < sortedSamples.size(); ++i)
                {
                    worstFrametimesSum += sortedSamples[i];
                }
                float avgWorstFrametime = worstFrametimesSum / static_cast<float>(low1pCount);
                if (avgWorstFrametime > 0.0f)
                {
                    ctx->displayed1PercentLowFps = 1000.0f / avgWorstFrametime;
                }
            }
            else
            {
                ctx->displayed1PercentLowFps = ctx->displayedFps;
            }

            ctx->fpsAccumulatedFrames = 0;
            ctx->fpsAccumulatedTimeMs = 0.0f;
            ctx->cpuAccumulatedTimeMs = 0.0f;
            ctx->gpuAccumulatedTimeMs = 0.0f;
        }
    }

    // -------------------------------------------------------------------------------------
    // 8.3 Atualização Periódica de Hardware (CPU, RAM e GPU)
    // -------------------------------------------------------------------------------------
    float sysElapsedMs = std::chrono::duration<float, std::milli>(now - ctx->lastSysQueryTime).count();
    if (sysElapsedMs >= 500.0f)
    {
        ctx->cachedCpuUsage = ctx->cpu.query();
        ctx->cachedRamStats = QueryRamStats();
        ctx->lastSysQueryTime = now;
    }

    float nvmlElapsedMs = std::chrono::duration<float, std::milli>(now - ctx->lastNvmlQueryTime).count();
    if (nvmlElapsedMs >= 300.0f)
    {
        ctx->cachedGpuStats = ctx->nvml.query();
        ctx->lastNvmlQueryTime = now;
    }

    // -------------------------------------------------------------------------------------
    // 8.4 Renderização no Swapchain OpenXR com Cores Progressivas
    // -------------------------------------------------------------------------------------
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t imageIndex = 0;
    XrResult acqRes = ctx->dispatch->pfnAcquireSwapchainImage(ctx->overlaySwapchain, &acquireInfo, &imageIndex);
    if (XR_SUCCEEDED(acqRes) && imageIndex < ctx->rtvList.size())
    {
        XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        ctx->dispatch->pfnWaitSwapchainImage(ctx->overlaySwapchain, &waitInfo);

        ID3D11RenderTargetView* rtv = ctx->rtvList[imageIndex].Get();
        if (rtv)
        {
            const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            ctx->d3d11Context->ClearRenderTargetView(rtv, clearColor);

            D3D11_VIEWPORT vp{};
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = 0.0f;
            vp.Width = static_cast<FLOAT>(ctx->overlayWidth);
            vp.Height = static_cast<FLOAT>(ctx->overlayHeight);
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;

            ctx->d3d11Context->RSSetViewports(1, &vp);
            ctx->d3d11Context->OMSetRenderTargets(1, &rtv, nullptr);

            ImGui::SetCurrentContext(ctx->imguiContext);
            ImGui_ImplDX11_NewFrame();
            ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(ctx->overlayWidth), static_cast<float>(ctx->overlayHeight));
            ImGui::NewFrame();

            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(ctx->overlayWidth), static_cast<float>(ctx->overlayHeight)));
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoCollapse;

            if (ImGui::Begin("##OpenXRPerformanceOverlayWindow", nullptr, flags))
            {
                // Cabeçalho com indicador de escala
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "OpenXR Overlay");
                ImGui::SameLine();
                ImGui::TextDisabled("| WMR");
                ImGui::SameLine(280.0f);
                ImGui::TextDisabled("[Ctrl+Shift+O] [%.0f%%]", ctx->overlayScale * 100.0f);
                ImGui::Separator();

                ImGui::Columns(2, "OverlayCols", false);
                ImGui::SetColumnWidth(0, 248.0f);

                // =========================================================================
                // --- COLUNA DA ESQUERDA: PROCESSADOR (CPU), RAM E TEMPOS ---
                // =========================================================================
                ImVec4 fpsColor = (ctx->displayedFps >= 85.0f) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
                                  (ctx->displayedFps >= 55.0f) ? ImVec4(1.0f, 0.9f, 0.2f, 1.0f) :
                                                                 ImVec4(1.0f, 0.25f, 0.25f, 1.0f);

                ImVec4 low1pColor = (ctx->displayed1PercentLowFps >= 80.0f) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
                                    (ctx->displayed1PercentLowFps >= 55.0f) ? ImVec4(1.0f, 0.9f, 0.2f, 1.0f) :
                                                                              ImVec4(1.0f, 0.25f, 0.25f, 1.0f);

                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "--- CPU & SISTEMA ---");

                // Linha de FPS: Atual, Médio e 1% Low
                ImGui::Text("FPS:");
                ImGui::SameLine();
                ImGui::TextColored(fpsColor, "%.1f", ctx->displayedFps);
                ImGui::SameLine(72.0f);
                ImGui::Text("Med:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "%.1f", ctx->displayedAvgFps);
                ImGui::SameLine(158.0f);
                ImGui::Text("1%%:");
                ImGui::SameLine();
                ImGui::TextColored(low1pColor, "%.1f", ctx->displayed1PercentLowFps);

                // Linha de Tempos: Frame Total e CPU Frame
                ImGui::Text("Total:");
                ImGui::SameLine();
                ImGui::TextColored(fpsColor, "%.1f ms", ctx->displayedFrameTimeMs);
                ImGui::SameLine(120.0f);
                ImGui::Text("CPU:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%.1f ms", ctx->displayedCpuFrameTimeMs);

                // Barra de Uso da CPU com Cor Progressiva Dinâmica
                float cpuUtilRatio = std::clamp(ctx->cachedCpuUsage / 100.0f, 0.0f, 1.0f);
                char cpuBuf[32];
                snprintf(cpuBuf, sizeof(cpuBuf), "CPU: %.1f %%", ctx->cachedCpuUsage);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GetProgressColor(cpuUtilRatio));
                ImGui::ProgressBar(cpuUtilRatio, ImVec2(235.0f, 15.0f), cpuBuf);
                ImGui::PopStyleColor();

                // Barra de Memória RAM com Cor Progressiva Dinâmica
                float ramRatio = (ctx->cachedRamStats.totalBytes > 0) ?
                    std::clamp(static_cast<float>(ctx->cachedRamStats.usedBytes) / static_cast<float>(ctx->cachedRamStats.totalBytes), 0.0f, 1.0f) : 0.0f;
                double ramUsedGB = static_cast<double>(ctx->cachedRamStats.usedBytes) / (1024.0 * 1024.0 * 1024.0);
                double ramTotalGB = static_cast<double>(ctx->cachedRamStats.totalBytes) / (1024.0 * 1024.0 * 1024.0);
                char ramBuf[48];
                snprintf(ramBuf, sizeof(ramBuf), "RAM: %.1f/%.1f GB (%.0f%%)", ramUsedGB, ramTotalGB, ramRatio * 100.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GetProgressColor(ramRatio));
                ImGui::ProgressBar(ramRatio, ImVec2(235.0f, 15.0f), ramBuf);
                ImGui::PopStyleColor();

                // Mini gráfico de Frametime
                ImGui::PlotLines("##Graph", ctx->frameTimeHistory, 60, ctx->historyIndex,
                                 nullptr, 0.0f, 33.3f, ImVec2(235.0f, 32.0f));

                ImGui::NextColumn();

                // =========================================================================
                // --- COLUNA DA DIREITA: PLACA DE VÍDEO (VGA / GPU NVIDIA) ---
                // =========================================================================
                if (ctx->cachedGpuStats.available)
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "--- VGA: %s ---", ctx->cachedGpuStats.name.c_str());

                    // Tempo de Quadro da GPU (em ms)
                    ImVec4 gpuFrameColor = (ctx->displayedGpuFrameTimeMs <= 9.0f) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
                                           (ctx->displayedGpuFrameTimeMs <= 11.1f) ? ImVec4(1.0f, 0.9f, 0.2f, 1.0f) :
                                                                                    ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
                    ImGui::Text("GPU Frame:");
                    ImGui::SameLine();
                    ImGui::TextColored(gpuFrameColor, "%.1f ms", ctx->displayedGpuFrameTimeMs);
                    ImGui::SameLine(140.0f);
                    ImVec4 tempColor = (ctx->cachedGpuStats.temperatureC < 75) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
                                       (ctx->cachedGpuStats.temperatureC < 83) ? ImVec4(1.0f, 0.7f, 0.2f, 1.0f) :
                                                                                 ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
                    ImGui::Text("Temp:");
                    ImGui::SameLine();
                    ImGui::TextColored(tempColor, "%u C", ctx->cachedGpuStats.temperatureC);

                    // Barra de Uso da GPU com Cor Progressiva Dinâmica
                    float gpuUtilRatio = std::clamp(static_cast<float>(ctx->cachedGpuStats.utilization) / 100.0f, 0.0f, 1.0f);
                    char gpuBuf[32];
                    snprintf(gpuBuf, sizeof(gpuBuf), "Uso GPU: %u %%", ctx->cachedGpuStats.utilization);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GetProgressColor(gpuUtilRatio));
                    ImGui::ProgressBar(gpuUtilRatio, ImVec2(235.0f, 16.0f), gpuBuf);
                    ImGui::PopStyleColor();

                    // Barra de VRAM com Cor Progressiva Dinâmica
                    double vramUsedGB = static_cast<double>(ctx->cachedGpuStats.memoryUsedBytes) / (1024.0 * 1024.0 * 1024.0);
                    double vramTotalGB = static_cast<double>(ctx->cachedGpuStats.memoryTotalBytes) / (1024.0 * 1024.0 * 1024.0);
                    float vramRatio = (vramTotalGB > 0.0) ? std::clamp(static_cast<float>(vramUsedGB / vramTotalGB), 0.0f, 1.0f) : 0.0f;
                    char vramBuf[48];
                    snprintf(vramBuf, sizeof(vramBuf), "VRAM: %.1f/%.1f GB (%.0f%%)", vramUsedGB, vramTotalGB, vramRatio * 100.0f);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GetProgressColor(vramRatio));
                    ImGui::ProgressBar(vramRatio, ImVec2(235.0f, 16.0f), vramBuf);
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "--- VGA / NVML Desconectado ---");
                    ImGui::TextDisabled("(Driver NVIDIA nao detectado)");
                }

                ImGui::Columns(1);
                ImGui::End();
            }

            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        ctx->dispatch->pfnReleaseSwapchainImage(ctx->overlaySwapchain, &releaseInfo);
    }

    // -------------------------------------------------------------------------------------
    // 8.5 Injeção do Quad Layer no xrEndFrame (Espaço 3D LOCAL Fixo estilo fpsVR)
    // -------------------------------------------------------------------------------------
    XrCompositionLayerQuad overlayQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    overlayQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    overlayQuad.space = ctx->overlaySpace; // Espaço LOCAL (Fixo no cockpit 3D)
    overlayQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    overlayQuad.subImage.swapchain = ctx->overlaySwapchain;
    overlayQuad.subImage.imageRect.offset = { 0, 0 };
    overlayQuad.subImage.imageRect.extent = { static_cast<int32_t>(ctx->overlayWidth), static_cast<int32_t>(ctx->overlayHeight) };
    overlayQuad.subImage.imageArrayIndex = 0;

    // =====================================================================================
    // PARÂMETROS DE POSICIONAMENTO 3D DO OVERLAY (Fixo no Cockpit estilo fpsVR)
    // =====================================================================================
    // - posX: Deslocamento lateral em metros (0.0m = Centralizado)
    // - posY: Altura em metros (-0.22m = Rodapé confortável do campo de visão, bem acima do chão)
    // - posZ: Distância à frente em metros (-0.75m = 75 cm à frente dos olhos)
    // - pitchDegrees: Inclinação (-20.0° = Inclinado suavemente para CIMA na direção dos olhos)
    // - widthMeters / heightMeters: Tamanho base multiplicado pela escala dinâmica
    // =====================================================================================
    const float posX = 0.0f;
    const float posY = -0.22f;
    const float posZ = -0.75f;
    const float pitchDegrees = -20.0f; // Inclinado suavemente para cima em direção aos olhos
    const float baseWidthMeters = 0.34f;
    const float baseHeightMeters = 0.17f;

    const float actualWidth = baseWidthMeters * ctx->overlayScale;
    const float actualHeight = baseHeightMeters * ctx->overlayScale;

    // Conversão do ângulo de inclinação (Pitch) para Quaternion do OpenXR
    const float pitchRad = pitchDegrees * (3.14159265359f / 180.0f);
    const float halfRad = pitchRad * 0.5f;

    overlayQuad.pose.orientation = { std::sin(halfRad), 0.0f, 0.0f, std::cos(halfRad) };
    overlayQuad.pose.position = { posX + ctx->overlayOffsetX, posY + ctx->overlayOffsetY, posZ };
    overlayQuad.size = { actualWidth, actualHeight };

    std::vector<const XrCompositionLayerBaseHeader*> combinedLayers;
    combinedLayers.reserve((frameEndInfo->layers ? frameEndInfo->layerCount : 0) + 1);

    if (frameEndInfo->layers != nullptr)
    {
        for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i)
        {
            if (frameEndInfo->layers[i] != nullptr)
            {
                combinedLayers.push_back(frameEndInfo->layers[i]);
            }
        }
    }

    combinedLayers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&overlayQuad));

    XrFrameEndInfo modifiedEndInfo = *frameEndInfo;
    modifiedEndInfo.layerCount = static_cast<uint32_t>(combinedLayers.size());
    modifiedEndInfo.layers = combinedLayers.data();

    return ctx->dispatch->pfnEndFrame(session, &modifiedEndInfo);
}
