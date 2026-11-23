@echo off
chcp 65001 >nul
setlocal

set "STARTUP_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "SHORTCUT_PATH=%STARTUP_DIR%\MiniDeckBridge.lnk"

echo.
if exist "%SHORTCUT_PATH%" (
    del /f /q "%SHORTCUT_PATH%"
    if exist "%SHORTCUT_PATH%" (
        echo ====================================================
        echo  [ERRO] Nao foi possivel remover o atalho:
        echo  %SHORTCUT_PATH%
        echo ====================================================
        echo.
        if /i not "%~1"=="--nopause" pause
        exit /b 1
    )
    echo ====================================================
    echo  [OK] Autostart do Mini Deck Bridge removido!
    echo  Ele nao iniciara mais automaticamente com o Windows.
    echo ====================================================
    echo.
    echo  Removido: %SHORTCUT_PATH%
) else (
    echo ====================================================
    echo  [INFO] Nenhum atalho encontrado no Startup do Windows.
    echo  O autostart ja estava desativado.
    echo ====================================================
)
echo.

if /i not "%~1"=="--nopause" pause
exit /b 0
