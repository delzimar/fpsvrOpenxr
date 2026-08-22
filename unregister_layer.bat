@echo off
setlocal

echo =========================================================
echo  Remocao do Registro da OpenXR API Layer
echo =========================================================
echo.

set "SCRIPT_DIR=%~dp0"
set "JSON_PATH=%SCRIPT_DIR%XR_APILAYER_NOVENDOR_performance_overlay.json"

reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "%JSON_PATH%" /f >nul 2>&1
reg delete "HKCU\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "%JSON_PATH%" /f >nul 2>&1

echo [INFO] Registros da API Layer removidos do sistema.
echo.
pause
