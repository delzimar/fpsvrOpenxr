@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo  Registro da OpenXR API Layer: OpenXR Performance Overlay
echo =========================================================
echo.

:: Obtem o caminho absoluto do diretorio atual
set "SCRIPT_DIR=%~dp0"
set "JSON_PATH=%SCRIPT_DIR%XR_APILAYER_NOVENDOR_performance_overlay.json"

if not exist "%JSON_PATH%" (
    echo [ERRO] O arquivo de manifesto "%JSON_PATH%" nao foi encontrado!
    pause
    exit /b 1
)

echo Registrando no Registro do Windows (Khronos OpenXR Implicit ApiLayers)...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "%JSON_PATH%" /t REG_DWORD /d 0 /f

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCESSO] API Layer registrada com sucesso!
    echo O Windows Mixed Reality e qualquer aplicacao OpenXR agora carregarao o overlay.
) else (
    echo.
    echo [AVISO] Falha ao registrar em HKLM (requer Administrador).
    echo Tentando registrar em HKCU (Current User)...
    reg add "HKCU\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "%JSON_PATH%" /t REG_DWORD /d 0 /f
    if !ERRORLEVEL! equ 0 (
        echo [SUCESSO] API Layer registrada com sucesso no escopo do usuario atual (HKCU)!
    ) else (
        echo [ERRO] Execute este script como Administrador (Botao direito -> Executar como Administrador).
    )
)

echo.
pause
