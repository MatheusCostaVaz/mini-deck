@echo off
chcp 65001 >nul
setlocal

set "PROJECT_DIR=%~dp0"
set "BACKEND_DIR=%PROJECT_DIR%backend"
set "VBS_PATH=%BACKEND_DIR%\iniciar_silencioso.vbs"
set "PYTHONW_PATH=%BACKEND_DIR%\.venv\Scripts\pythonw.exe"
set "BRIDGE_PATH=%BACKEND_DIR%\wifi_bridge.py"
set "STARTUP_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "SHORTCUT_PATH=%STARTUP_DIR%\MiniDeckBridge.lnk"

echo.
echo Instalando Mini Deck Bridge no Autostart do Windows...

if not exist "%VBS_PATH%" (
    echo [ERRO] Arquivo nao encontrado: "%VBS_PATH%"
    goto :erro
)
if not exist "%PYTHONW_PATH%" (
    echo [ERRO] Ambiente virtual nao encontrado: "%PYTHONW_PATH%"
    echo        Execute "python -m venv backend\.venv" e instale backend\requirements.txt.
    goto :erro
)
if not exist "%BRIDGE_PATH%" (
    echo [ERRO] Arquivo nao encontrado: "%BRIDGE_PATH%"
    goto :erro
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $sc = $ws.CreateShortcut($env:SHORTCUT_PATH); $sc.TargetPath = $env:VBS_PATH; $sc.WorkingDirectory = $env:BACKEND_DIR; $sc.Description = 'Mini Deck IoT Wi-Fi Bridge (Silencioso)'; $sc.Save()"
if errorlevel 1 (
    echo [ERRO] Falha ao criar o atalho na pasta Startup do Windows.
    goto :erro
)

if not exist "%SHORTCUT_PATH%" (
    echo [ERRO] Atalho nao encontrado apos criacao: "%SHORTCUT_PATH%"
    goto :erro
)

start "" wscript.exe "%VBS_PATH%"

echo.
echo ====================================================
echo  [SUCESSO] Mini Deck Bridge instalado com sucesso!
echo  Ele agora iniciara automaticamente junto com o Windows.
echo ====================================================
echo.
echo  Atalho : %SHORTCUT_PATH%
echo  Origem : %VBS_PATH%
echo  Log    : %BACKEND_DIR%\wifi_bridge.log
echo.

if /i not "%~1"=="--nopause" pause
exit /b 0

:erro
echo.
echo ====================================================
echo  [FALHA] Nao foi possivel concluir a instalacao.
echo ====================================================
echo.
if /i not "%~1"=="--nopause" pause
exit /b 1
