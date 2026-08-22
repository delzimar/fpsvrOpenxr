#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <filesystem>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// Constantes
static const wchar_t* REG_KEY_OPENXR_IMPLICIT = L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
static const wchar_t* MANIFEST_FILENAME = L"XR_APILAYER_NOVENDOR_performance_overlay.json";

// IDs dos Controles
#define IDC_BTN_ENABLE      101
#define IDC_BTN_DISABLE     102
#define IDC_BTN_TEST_LOG    103
#define IDC_BTN_ABOUT       104

// Variáveis Globais
HINSTANCE g_hInstance = nullptr;
HWND g_hWndStatus = nullptr;
HWND g_hBtnEnable = nullptr;
HWND g_hBtnDisable = nullptr;
HFONT g_hFontTitle = nullptr;
HFONT g_hFontNormal = nullptr;
HFONT g_hFontBold = nullptr;

std::wstring GetCurrentDirectoryWString()
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    size_t pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(0, pos) : L"";
}

std::wstring GetManifestFullPath()
{
    return GetCurrentDirectoryWString() + L"\\" + MANIFEST_FILENAME;
}

bool IsLayerRegistered()
{
    std::wstring manifestPath = GetManifestFullPath();
    HKEY hKey = nullptr;

    // Checa no HKLM
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_KEY_OPENXR_IMPLICIT, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwType = 0;
        DWORD dwValue = 0;
        DWORD dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, manifestPath.c_str(), nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwValue), &dwSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return (dwValue == 0); // 0 = ativado no padrão OpenXR
        }
        RegCloseKey(hKey);
    }

    // Checa no HKCU
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_OPENXR_IMPLICIT, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwType = 0;
        DWORD dwValue = 0;
        DWORD dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, manifestPath.c_str(), nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwValue), &dwSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return (dwValue == 0);
        }
        RegCloseKey(hKey);
    }

    return false;
}

bool RegisterLayer()
{
    std::wstring manifestPath = GetManifestFullPath();
    HKEY hKey = nullptr;
    DWORD dwDisposition = 0;

    LSTATUS status = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        REG_KEY_OPENXR_IMPLICIT,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &hKey,
        &dwDisposition);

    if (status != ERROR_SUCCESS)
    {
        // Se falhar no HKLM por falta de admin, grava no HKCU
        status = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            REG_KEY_OPENXR_IMPLICIT,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            nullptr,
            &hKey,
            &dwDisposition);
    }

    if (status == ERROR_SUCCESS)
    {
        DWORD dwValue = 0; // 0 = ativado
        RegSetValueExW(hKey, manifestPath.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwValue), sizeof(DWORD));
        RegCloseKey(hKey);
        return true;
    }

    return false;
}

bool UnregisterLayer()
{
    std::wstring manifestPath = GetManifestFullPath();
    HKEY hKey = nullptr;
    bool success = false;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_KEY_OPENXR_IMPLICIT, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, manifestPath.c_str());
        RegCloseKey(hKey);
        success = true;
    }

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_OPENXR_IMPLICIT, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, manifestPath.c_str());
        RegCloseKey(hKey);
        success = true;
    }

    return success;
}

void UpdateStatusUI(HWND hwnd)
{
    bool active = IsLayerRegistered();
    if (g_hWndStatus)
    {
        if (active)
        {
            SetWindowTextW(g_hWndStatus, L"STATUS: ATIVADO E PRONTO PARA VR (AMS2, AC, WMR, etc.)");
        }
        else
        {
            SetWindowTextW(g_hWndStatus, L"STATUS: DESATIVADO (Desconectado do OpenXR / Seguro para EAC)");
        }
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hFontTitle = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontBold = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        // Botões Principais
        g_hBtnEnable = CreateWindowW(
            L"BUTTON", L"ATIVAR OVERLAY NO OPENXR",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            30, 95, 250, 45, hwnd, reinterpret_cast<HMENU>(IDC_BTN_ENABLE), g_hInstance, nullptr);
        SendMessageW(g_hBtnEnable, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        g_hBtnDisable = CreateWindowW(
            L"BUTTON", L"DESATIVAR OVERLAY (Para EAC/LMU)",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            300, 95, 250, 45, hwnd, reinterpret_cast<HMENU>(IDC_BTN_DISABLE), g_hInstance, nullptr);
        SendMessageW(g_hBtnDisable, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Status
        g_hWndStatus = CreateWindowW(
            L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            30, 155, 520, 25, hwnd, nullptr, g_hInstance, nullptr);
        SendMessageW(g_hWndStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

        // Botão Abrir Log
        HWND hBtnLog = CreateWindowW(
            L"BUTTON", L"Ver Arquivo de Log de Diagnostico",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            170, 395, 240, 30, hwnd, reinterpret_cast<HMENU>(IDC_BTN_TEST_LOG), g_hInstance, nullptr);
        SendMessageW(hBtnLog, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

        UpdateStatusUI(hwnd);
        break;
    }
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDC_BTN_ENABLE:
            if (RegisterLayer())
            {
                MessageBoxW(hwnd, L"Overlay OpenXR ATIVADO com sucesso!\n\nEle agora carregara automaticamente em qualquer simulador OpenXR (AMS2, Assetto Corsa, etc.).", L"OpenXR Overlay", MB_ICONINFORMATION | MB_OK);
            }
            else
            {
                MessageBoxW(hwnd, L"Falha ao registrar no Windows. Tente executar como Administrador.", L"Erro", MB_ICONERROR | MB_OK);
            }
            UpdateStatusUI(hwnd);
            break;

        case IDC_BTN_DISABLE:
            UnregisterLayer();
            MessageBoxW(hwnd, L"Overlay OpenXR DESATIVADO com sucesso!\n\nAgora voce pode abrir o Le Mans Ultimate (LMU) ou jogos com EAC sem conflitos.", L"OpenXR Overlay", MB_ICONINFORMATION | MB_OK);
            UpdateStatusUI(hwnd);
            break;

        case IDC_BTN_TEST_LOG:
        {
            ShellExecuteW(nullptr, L"open", L"notepad.exe", L"C:\\Users\\DVGA\\AppData\\Local\\Temp\\OpenXR_Performance_Overlay.log", nullptr, SW_SHOW);
            break;
        }
        }
        break;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fundo
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        HBRUSH hBgBrush = CreateSolidBrush(RGB(245, 247, 250));
        FillRect(hdc, &rcClient, hBgBrush);
        DeleteObject(hBgBrush);

        SetBkMode(hdc, TRANSPARENT);

        // Título Principal
        SelectObject(hdc, g_hFontTitle);
        SetTextColor(hdc, RGB(20, 40, 80));
        TextOutW(hdc, 30, 20, L"OpenXR Performance Overlay Manager", 35);

        // Subtítulo
        SelectObject(hdc, g_hFontNormal);
        SetTextColor(hdc, RGB(100, 110, 120));
        TextOutW(hdc, 30, 52, L"Gerenciador de Telemetria de Hardware para VR (Samsung WMR / NVIDIA)", 71);

        // Linha divisória
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(215, 220, 228));
        SelectObject(hdc, hPen);
        MoveToEx(hdc, 30, 190, nullptr);
        LineTo(hdc, 550, 190);

        // Seção de Atalhos e Instruções
        SelectObject(hdc, g_hFontBold);
        SetTextColor(hdc, RGB(30, 50, 90));
        TextOutW(hdc, 30, 205, L"Atalhos de Teclado no Cockpit VR:", 33);

        SelectObject(hdc, g_hFontNormal);
        SetTextColor(hdc, RGB(40, 40, 40));
        TextOutW(hdc, 40, 235, L"- Ctrl + Shift + O :  Ligar / Desligar o Overlay no headset", 60);
        TextOutW(hdc, 40, 260, L"- Ctrl + Shift + Setas :  Mover a posicao 3D no cockpit (Cima/Baixo/Esq/Dir)", 74);
        TextOutW(hdc, 40, 285, L"- Ctrl + Shift + (+) / (-) :  Aumentar ou Diminuir tamanho do painel", 67);

        // Compatibilidade
        SelectObject(hdc, g_hFontBold);
        SetTextColor(hdc, RGB(30, 50, 90));
        TextOutW(hdc, 30, 320, L"Compatibilidade:", 16);

        SelectObject(hdc, g_hFontNormal);
        SetTextColor(hdc, RGB(60, 60, 60));
        TextOutW(hdc, 40, 345, L"- Todos os headsets WMR (Samsung Odyssey/+, HP Reverb G2, Lenovo, etc.)", 71);
        TextOutW(hdc, 40, 368, L"- GPUs NVIDIA GeForce (Uso GPU, VRAM, Temperatura, CPU Frame Time)", 67);

        DeleteObject(hPen);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = reinterpret_cast<HDC>(wParam);
        HWND hwndStatic = reinterpret_cast<HWND>(lParam);
        if (hwndStatic == g_hWndStatus)
        {
            bool active = IsLayerRegistered();
            if (active)
            {
                SetTextColor(hdcStatic, RGB(0, 140, 40)); // Verde
            }
            else
            {
                SetTextColor(hdcStatic, RGB(180, 40, 40)); // Vermelho
            }
            SetBkMode(hdcStatic, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
        }
        break;
    }
    case WM_DESTROY:
        if (g_hFontTitle) DeleteObject(g_hFontTitle);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    g_hInstance = hInstance;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"OpenXROverlayManagerClass";

    if (!RegisterClassExW(&wc))
        return 0;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 600;
    int winH = 480;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"OpenXROverlayManagerClass",
        L"OpenXR Performance Overlay Manager v1.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        winX, winY, winW, winH,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
        return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
