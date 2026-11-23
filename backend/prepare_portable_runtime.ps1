#Requires -Version 5.1
<#
.SYNOPSIS
    Monta o runtime Python portatil e assinado (Python Embeddable Package + site-packages)
    em backend\runtime_portable\ para empacotamento no MiniDeckSetup.exe via Inno Setup.
#>

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
Add-Type -AssemblyName System.IO.Compression.FileSystem

$BackendDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$VenvPy       = Join-Path $BackendDir ".venv\Scripts\python.exe"
$VenvSite     = Join-Path $BackendDir ".venv\Lib\site-packages"
$BuildDir     = Join-Path $BackendDir "build"
$RuntimeDir   = Join-Path $BackendDir "runtime_portable"

if (-not (Test-Path $VenvPy)) {
    throw "Ambiente virtual nao encontrado em '$VenvPy'. Crie o .venv e instale backend\requirements.txt antes de compilar o instalador."
}

# 1. Descobrir versao exata do Python do .venv (ex.: 3.14.6)
$PyInfo = & $VenvPy -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}|{sys.version_info.major}{sys.version_info.minor}')"
if ($LASTEXITCODE -ne 0 -or -not $PyInfo) {
    throw "Falha ao detectar versao do Python em '$VenvPy'."
}
$Parts = $PyInfo.Trim().Split("|")
$PyVersion = $Parts[0]
$PyTag     = $Parts[1]

Write-Host "[1/4] Python detectado no .venv: $PyVersion (python$PyTag)" -ForegroundColor Cyan

# 2. Baixar Python Embeddable Package oficial (assinado pela Python Software Foundation)
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
$EmbedZipName = "python-$PyVersion-embed-amd64.zip"
$EmbedZipPath = Join-Path $BuildDir $EmbedZipName
$EmbedUrl     = "https://www.python.org/ftp/python/$PyVersion/$EmbedZipName"

if (-not (Test-Path $EmbedZipPath)) {
    Write-Host "[2/4] Baixando runtime oficial assinado: $EmbedUrl" -ForegroundColor Cyan
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $EmbedUrl -OutFile $EmbedZipPath -UseBasicParsing
} else {
    Write-Host "[2/4] Usando cache local do runtime: $EmbedZipPath" -ForegroundColor Green
}

# 3. Extrair e configurar ._pth para modo portatil
Write-Host "[3/4] Montando runtime portatil em $RuntimeDir ..." -ForegroundColor Cyan
if (Test-Path $RuntimeDir) {
    Remove-Item -Path $RuntimeDir -Recurse -Force
}
New-Item -ItemType Directory -Path $RuntimeDir -Force | Out-Null
[System.IO.Compression.ZipFile]::ExtractToDirectory($EmbedZipPath, $RuntimeDir)

$PthFile = Join-Path $RuntimeDir "python$PyTag._pth"
$PthContent = @(
    "python$PyTag.zip"
    "."
    ".."
    "Lib\site-packages"
    "import site"
) -join "`r`n"
[System.IO.File]::WriteAllText($PthFile, $PthContent + "`r`n", (New-Object System.Text.UTF8Encoding($false)))

# 4. Copiar apenas dependencias de execucao do site-packages (excluindo ferramentas de build)
$TargetSite = Join-Path $RuntimeDir "Lib\site-packages"
New-Item -ItemType Directory -Path $TargetSite -Force | Out-Null

$ExcludePatterns = @(
    "^pip($|[-_.])",
    "^setuptools($|[-_.])",
    "^wheel($|[-_.])",
    "^pkg_resources$",
    "^_distutils_hack$",
    "^distutils-precedence\.pth$",
    "^PyInstaller($|[-_.])",
    "^pyinstaller($|[-_.])",
    "^_pyinstaller_hooks_contrib$",
    "^pyinstaller_hooks_contrib($|[-_.])",
    "^altgraph($|[-_.])",
    "^pefile($|[-_.])",
    "^__pycache__$"
)

Get-ChildItem -Path $VenvSite -Force | ForEach-Object {
    $name = $_.Name
    $skip = $false
    foreach ($pat in $ExcludePatterns) {
        if ($name -imatch $pat) {
            $skip = $true
            break
        }
    }
    if (-not $skip) {
        Copy-Item -Path $_.FullName -Destination (Join-Path $TargetSite $name) -Recurse -Force
    }
}

# Limpar caches __pycache__ para reduzir tamanho do instalador
Get-ChildItem -Path $TargetSite -Directory -Recurse -Filter "__pycache__" -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

# 5. Validacao de assinatura digital e smoke-test real dos modulos da bridge
Write-Host "[4/4] Validando assinatura digital e imports no runtime portatil..." -ForegroundColor Cyan
$PortablePyW = Join-Path $RuntimeDir "pythonw.exe"
$PortablePy  = Join-Path $RuntimeDir "python.exe"

$Sig = Get-AuthenticodeSignature $PortablePyW
if ($Sig.Status -ne "Valid") {
    throw "Assinatura digital de '$PortablePyW' invalida: $($Sig.Status)"
}
Write-Host "      Assinatura OK: $($Sig.SignerCertificate.Subject)" -ForegroundColor Green

$SmokeCode = @"
import sys
import psutil
import websockets
import comtypes
import pycaw
import audio_controller
import media_controller
import wifi_bridge
from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager
from winrt.windows.devices.radios import Radio
print('PORTABLE_RUNTIME_OK', sys.executable)
"@

& $PortablePy -c $SmokeCode
if ($LASTEXITCODE -ne 0) {
    throw "Falha no smoke-test de imports do runtime portatil!"
}

$SizeMB = [math]::Round(((Get-ChildItem -Path $RuntimeDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB), 2)
Write-Host "Runtime portatil pronto ($SizeMB MB) em: $RuntimeDir" -ForegroundColor Green
