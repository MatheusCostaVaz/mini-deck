@echo off
setlocal EnableExtensions
chcp 65001 >nul

echo ================================================================
echo   MINI DECK BRIDGE v2.3 - GERADOR DO INSTALADOR (MiniDeckSetup)
echo ================================================================
echo.

set "PROJECT_DIR=%~dp0"
set "ISS_FILE=%PROJECT_DIR%backend\installer.iss"
set "RUNTIME_SCRIPT=%PROJECT_DIR%backend\prepare_portable_runtime.ps1"
set "OUTPUT_EXE=%PROJECT_DIR%dist\MiniDeckSetup.exe"
set "ISCC_EXE="

:: 1. Localizar Inno Setup Compiler (ISCC.exe)
call :FindISCC

if not defined ISCC_EXE (
    echo [!] Inno Setup Compiler ^(ISCC.exe^) nao encontrado.
    echo [*] Tentando instalar automaticamente via winget ^(JRSoftware.InnoSetup^)...
    echo.
    where winget >nul 2>&1
    if errorlevel 1 (
        echo [ERRO] winget nao encontrado neste sistema.
        echo        Instale o Inno Setup 6 manualmente em: https://jrsoftware.org/isdl.php
        goto :Fail
    )

    winget install --id JRSoftware.InnoSetup -e --source winget --accept-package-agreements --accept-source-agreements
    if errorlevel 1 (
        echo [ERRO] Falha ao instalar Inno Setup via winget.
        echo        Instale manualmente em: https://jrsoftware.org/isdl.php
        goto :Fail
    )

    call :FindISCC
    if not defined ISCC_EXE (
        echo [ERRO] Inno Setup instalado, mas ISCC.exe nao foi localizado automaticamente.
        goto :Fail
    )
)

echo [OK] Inno Setup Compiler encontrado:
echo      "%ISCC_EXE%"
echo.

:: 2. Montar runtime Python portatil + dependencias + smoke-test
echo [*] Preparando runtime Python portatil assinado...
powershell -NoProfile -ExecutionPolicy Bypass -File "%RUNTIME_SCRIPT%"
if errorlevel 1 (
    echo.
    echo [ERRO] Falha ao montar o runtime Python portatil.
    goto :Fail
)
echo.

:: 3. Compilar o instalador Inno Setup
if not exist "%PROJECT_DIR%dist" mkdir "%PROJECT_DIR%dist"

echo [*] Compilando instalador "%ISS_FILE%"...
"%ISCC_EXE%" "%ISS_FILE%"
if errorlevel 1 (
    echo.
    echo [ERRO] Falha na compilacao do Inno Setup.
    goto :Fail
)
echo.

:: 4. Validar arquivo compilado dist\MiniDeckSetup.exe
if not exist "%OUTPUT_EXE%" (
    echo [ERRO] Instalador esperado nao foi encontrado em: "%OUTPUT_EXE%"
    goto :Fail
)

for %%A in ("%OUTPUT_EXE%") do set "FILE_SIZE=%%~zA"
if "%FILE_SIZE%"=="" set "FILE_SIZE=0"
if %FILE_SIZE% LEQ 0 (
    echo [ERRO] O arquivo gerado esta vazio: "%OUTPUT_EXE%"
    goto :Fail
)

set /a "FILE_SIZE_MB=%FILE_SIZE% / 1048576"

echo ================================================================
echo   INSTALADOR GERADO COM SUCESSO!
echo ================================================================
echo   Arquivo : %OUTPUT_EXE%
echo   Tamanho : ~%FILE_SIZE_MB% MB ^(%FILE_SIZE% bytes^)
echo ================================================================
echo.

if /I not "%~1"=="--no-pause" pause
exit /b 0

:FindISCC
for /f "delims=" %%I in ('where ISCC.exe 2^>nul') do (
    if not defined ISCC_EXE set "ISCC_EXE=%%I"
)
if not defined ISCC_EXE if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
exit /b 0

:Fail
echo.
if /I not "%~1"=="--no-pause" pause
exit /b 1
