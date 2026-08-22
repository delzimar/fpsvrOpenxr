# OpenXR Performance Overlay - V0.1 (API Layer DLL)

OpenXR API Layer de alta performance para monitoramento de hardware (GPU NVIDIA via NVML, FPS, Frame Time e VRAM) injetado diretamente como um painel flutuante VR (**`XrCompositionLayerQuad`**) no Windows Mixed Reality (WMR / Samsung Odyssey+) e em qualquer runtime OpenXR no Windows.

---

## 🎯 Arquitetura da Solução

```
+-------------------------------------------------------------+
|                     OpenXR Application                      |
|                 (Ex: Jogo VR / MSFS 2020 / etc.)            |
+-------------------------------------------------------------+
                              │
                              ▼
+-------------------------------------------------------------+
|                      OpenXR Loader                          |
+-------------------------------------------------------------+
                              │
                              ▼
+-------------------------------------------------------------+
|    [API LAYER] OpenXRPerformanceOverlay.dll                 |
|                                                             |
|  * xrNegotiateLoaderApiLayerInterface                       |
|  * Dispatch Table / Hooking Chaining                        |
|  * xrCreateSession (Captura D3D11Device, cria Swapchain)    |
|  * NvmlMonitor (GPU %, VRAM, Temperatura via nvml.dll)      |
|  * Dear ImGui (Renderiza painel no Swapchain D3D11)         |
|  * xrEndFrame (Injeta XrCompositionLayerQuad no espaço VIEW)|
+-------------------------------------------------------------+
                              │
                              ▼
+-------------------------------------------------------------+
|               OpenXR Runtime (WMR / SteamVR)                |
+-------------------------------------------------------------+
```

---

## 🛠️ Recursos Implementados

1. **OpenXR Loader Negotiation:**
   - Exportação correta da função `xrNegotiateLoaderApiLayerInterface`.
   - Sistema de chaining e *trampoline dispatch table* seguro com proteção de concorrência (`std::mutex`).
2. **Interceptação de Sessão (`xrCreateSession`):**
   - Extração transparente do `ID3D11Device` a partir de `XrGraphicsBindingD3D11KHR`.
   - Criação do espaço de referência fixado na cabeça (`XR_REFERENCE_SPACE_TYPE_VIEW`).
   - Criação do `XrSwapchain` dedicado para o overlay e instanciação de `ID3D11RenderTargetView`.
3. **Interface Gráfica com Dear ImGui & DirectX 11:**
   - Inicialização do backend `ImGui_ImplDX11`.
   - Painel HUD moderno com cores de alto contraste para visibilidade no VR.
   - Indicador de FPS, Tempo de Frame (ms), gráfico de histórico, Uso de GPU (%), VRAM (GB) e Temperatura (°C).
4. **Injeção de Camada Quad (`xrEndFrame`):**
   - Renderização isolada no swapchain do overlay sem corromper o pipeline D3D11 do jogo.
   - Injeção de `XrCompositionLayerQuad` com transparência alpha (`XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT`).
   - Posição fixa no campo de visão: `(x: 0.0m, y: -0.22m, z: -1.0m)` com dimensões `(36 cm x 18 cm)`.

---

## 📦 Como Compilar

1. Abra a solução `OpenXRPerformanceOverlay.sln` no Visual Studio.
2. Selecione a plataforma **`x64`** e configuração **`Release`** (ou **`Debug`**).
3. Pressione **Ctrl + Shift + B** (Compilar Solução).
4. A DLL `OpenXRPerformanceOverlay.dll` será gerada em `bin\Release\` e copiada automaticamente para a raiz do projeto.

---

## 🚀 Como Registrar no Windows

### Método Automático (Recomendado)
- Clique com o botão direito no arquivo **`register_layer.bat`** e selecione **"Executar como Administrador"**.
- Para desativar/remover, execute **`unregister_layer.bat`**.

### Método Manual via Regedit
Abra o `regedit.exe` e navegue até:
```
HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit
```
*(ou `HKEY_CURRENT_USER\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit`)*

- Crie um novo valor **DWORD (32-bit)**:
  - **Nome:** Caminho absoluto para o arquivo `XR_APILAYER_NOVENDOR_performance_overlay.json`
    *(Exemplo: `D:\Downloads\OpenXR_Performance_Overlay_V0.1\XR_APILAYER_NOVENDOR_performance_overlay.json`)*
  - **Dados do valor:** `0` (habilitado)

---

## 🔍 Depuração e Logs em Tempo Real

A API Layer emite todos os eventos de diagnóstico e erros em **Português** através da API `OutputDebugStringA`.

Para acompanhar os logs em tempo real:
1. Use o aplicativo gratuito **[Sysinternals DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview)** ou a janela de saída **Output** do Visual Studio ao depurar o executável do jogo.
2. Filtre pelas mensagens iniciadas com `[OpenXR_Performance_Overlay]`.
