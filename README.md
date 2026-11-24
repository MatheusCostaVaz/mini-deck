# 🎛️ Mini Deck IoT — Versão 2.3 Oficial

Dispositivo físico autônomo com **display touch IPS de 3.5" (480×320)** baseado em **ESP32-S3** (**Guition JC3248W535C** / compatível com a linha 3.5" ESP32-3248S035), desenvolvido para controle em tempo real de **áudio, telemetria contínua de hardware, mídia, atalhos, hub de jogos e configurações do Windows 11** via **Wi-Fi / WebSocket de ultra-baixa latência**.

---

## 🚀 Instalação Oficial no Windows (Instalador Único)

### ✅ Método Oficial: `MiniDeckSetup.exe` (Recomendado)
A forma oficial e mais simples de instalar a bridge em qualquer PC com Windows 10/11 (64-bit) — **sem precisar instalar Python**:

1. Execute o instalador único **`dist/MiniDeckSetup.exe`** (gerado via Inno Setup a partir de [`backend/installer.iss`](backend/installer.iss) e [`build_installer.bat`](build_installer.bat)).
2. O instalador configura tudo automaticamente em `%LOCALAPPDATA%\MiniDeck`:
   - **Sem UAC / sem permissão de Administrador** (`PrivilegesRequired=lowest`).
   - **Ambiente Python portátil embutido e oficialmente assinado** (Python Software Foundation) — **100% silencioso** (zero telas pretas no console) e **imune a falsos-positivos do Windows Smart App Control / Defender**.
   - **Inicialização automática com o Windows** via `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\MiniDeckBridge.lnk` apontando para [`backend/iniciar_silencioso.vbs`](backend/iniciar_silencioso.vbs).
   - **Atalhos no Menu Iniciar (e opcionais na Área de Trabalho):** Iniciar Bridge, Parar Bridge ([`backend/parar_bridge.vbs`](backend/parar_bridge.vbs)), Editar Jogos/Atalhos (`config.json`) e Desinstalar com limpeza automática.
   - Ao final do assistente, basta deixar marcada a opção **`[X] Iniciar o Mini Deck Bridge agora`** e clicar em **Concluir**.

### 📦 Como Gerar o Instalador a partir do Código-Fonte
Basta executar [`build_installer.bat`](build_installer.bat) na raiz do projeto:
1. Localiza automaticamente o **Inno Setup Compiler (`ISCC.exe`)** — ou instala o `JRSoftware.InnoSetup` via `winget` caso ainda não esteja presente.
2. Monta e valida o runtime Python portátil assinado via [`backend/prepare_portable_runtime.ps1`](backend/prepare_portable_runtime.ps1).
3. Compila [`backend/installer.iss`](backend/installer.iss) e valida o executável final em **`dist/MiniDeckSetup.exe`**.

---

## ✨ Funcionalidades da Versão 2.3

### 🎮 1. Hub de Jogos Dinâmico & Game HUD (`screen_games` + `screen_game_hud`)
- **Steam Auto-Scan:** localiza automaticamente a Steam via Registro do Windows (`HKCU\Software\Valve\Steam`), lê todas as bibliotecas em `libraryfolders.vdf` e importa todos os jogos instalados via `appmanifest_*.acf` (filtrando ferramentas internas como Steamworks, Proton e Redistributables).
- **Jogos Personalizados (`config.json`):** mescla jogos manuais/não-Steam (como **WARDOGS**) com a biblioteca Steam sem duplicatas e com **hot-reload automático** (basta salvar o arquivo para atualizar os botões no display sem regravar o ESP32).
- **Game HUD Dedicado:** telemetria ao vivo de CPU/GPU/RAM, slider lateral exclusivo para o **volume do jogo ativo**, botão discreto **`<`** no cabeçalho para retornar à lista de jogos e proteção via `psutil` contra reabertura de jogos já em execução.
- **CS2 GSI C4 Timer (40s — Ultra-Low Latency):** servidor HTTP local em `127.0.0.1:3000` com `buffer: 0.0`, `throttle: 0.0`, `TCP_NODELAY` no WebSocket e `WiFi.setSleep(false)` no ESP32, com alertas visuais críticos (< 10s / < 5s).

### 📊 2. Monitor de Hardware Contínuo com Anti-Standby (`screen_hardware`)
- Telemetria em tempo real (1,5s) com uso (`%`) e temperaturas (`°C`) de **CPU** (PDH/WMI) e **GPU NVIDIA** (`nvml.dll`, sem subprocessos), além de **RAM** (GB e `%`) e **Disco C:**, com alerta visual acima de 80°C.
- **Proteção Anti-Standby Inteligente:** o firmware bloqueia automaticamente a entrada no descanso de tela enquanto o display estiver em **`screen_hardware`**, **`screen_games`** ou **`screen_game_hud`**, permitindo manter o monitor de temperaturas ou o HUD do jogo sempre visíveis.

### ⚙️ 3. Configurações Modulares com PWM (`screen_settings`)
Hub 2×2 com telas dedicadas e confortáveis para toque:
- **📶 Wi-Fi (`screen_wifi_manager`):** sistema **Multi-Wi-Fi** com até **4 redes salvas na NVS** (`Preferences.h`), auto-seleção no boot pela rede conhecida com melhor RSSI, scanner assíncrono, teclado virtual LVGL completo (`480×240`) e fallback seguro de 12s em caso de falha.
- **ᛒ Bluetooth do PC:** controle bidirecional do rádio Bluetooth do Windows via WinRT (`Windows.Devices.Radios`).
- **💡 Tela & Brilho (`screen_brightness`):** slider horizontal (`10%–100%`) e presets rápidos (`25%`, `60%`, `100%`) com controle por hardware **PWM LEDC** (`GFX_BL` — GPIO 1 na Guition JC3248W535C / configurável para GPIO 27 em variantes ESP32-3248S035) persistido na NVS.
- **ℹ️ Sistema / Info (`screen_system_info`):** versão do firmware, Heap RAM / PSRAM / Flash livres ao vivo, IP do PC conectado, seletor de **Standby / Auto-Sleep** (`Desativado`, `2 min`, `5 min`, `10 min`), calibração touch e reinicialização segura (`ESP.restart()`).

### 🌙 4. Descanso de Tela Inteligente (`screen_screensaver`)
- Relógio digital grande (`48 px`), data em português via NTP (`GMT-3`) e **clima em tempo real de São Luís** (Open-Meteo API, atualizado a cada 30 min) com dimerização suave do brilho.
- Acionado automaticamente por inatividade real do PC (`GetLastInputInfo` > 5 min), suspensão/queda do WebSocket (> 20s) ou tempo de standby configurado — respeitando o bloqueio **Anti-Standby** nas telas de Hardware e Jogos.
- Desperta instantaneamente ao tocar na tela (toque consumido com segurança) ou ao mexer no mouse/teclado do PC.

### 🎚️ 5. Mixer de Áudio, Player de Mídia & Atalhos Configuráveis
- **Mixer de Áudio (`screen_audio`):** controle independente (0–100% + Mute) de **Master**, **Navegador**, **Voz/Discord** e **Jogo Ativo**, com recuperação automática **Hot-Plug WASAPI** ao conectar/desconectar fones Bluetooth ou headsets USB.
- **Player de Mídia (`screen_media`):** integração nativa com o **Windows Media Manager (WinRT SMTC)** exibindo faixa/vídeo, artista, controles **Anterior / Play-Pause / Próximo** e volume do navegador.
- **Atalhos Configuráveis (`screen_macros`):** grade dinâmica alimentada por `config.json` (apps locais, protocolos `steam://`/`discord://` e URLs).
- **Header Universal:** relógio NTP, botão **Home**, status de conexão e botão rápido de **Mute do Microfone** acessíveis em todas as telas.

---

## 📝 Configuração Dinâmica (`config.json`)

Edite `config.json` (pelo atalho **Editar Jogos e Atalhos** no Menu Iniciar ou em `backend/config.json`) e salve — as mudanças sincronizam em tempo real via hot-reload:

```json
{
  "games": [
    {
      "id": "cs2",
      "name": "CS2",
      "process": "cs2.exe",
      "launch": "steam://rungameid/730",
      "has_c4": true
    },
    {
      "id": "pubg",
      "name": "PUBG",
      "process": "TslGame.exe",
      "launch": "steam://rungameid/578080",
      "has_c4": false
    },
    {
      "id": "wardogs",
      "name": "WARDOGS",
      "process": "WardogsClient-Win64-Shipping.exe",
      "launch": "steam://rungameid/1867240",
      "has_c4": false
    }
  ],
  "macros": [
    { "id": "edge", "name": "EDGE", "target": "msedge.exe" },
    { "id": "steam", "name": "STEAM", "target": "steam.exe" },
    { "id": "discord", "name": "DISCORD", "target": "Discord.exe" },
    { "id": "disney", "name": "DISNEY+", "target": "https://www.disneyplus.com" },
    { "id": "max", "name": "MAX", "target": "https://www.max.com" }
  ]
}
```

---

## 🔐 Credenciais Wi-Fi Locais (`secrets.h`)

Nenhuma senha ou SSID fica exposto no Git (`firmware/include/secrets.h` está no `.gitignore`).
- Opcionalmente, copie [`firmware/include/secrets.h.example`](firmware/include/secrets.h.example) para `firmware/include/secrets.h` para definir uma rede padrão de fábrica.
- Sem `secrets.h`, basta cadastrar até 4 redes pelo teclado virtual em **⚙️ Configurações → Wi-Fi**.

---

## 🛠️ Setup de Desenvolvimento & Build

### 1. Preparar Ambiente Python & Gerar Instalador
```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
cd ..
.\build_installer.bat
```

### 2. Compilar e Gravar o Firmware ESP32-S3
```powershell
pio run -d firmware --target upload
```

### Stack & Tecnologias
- **Hardware:** Guition **JC3248W535C** (ESP32-S3-N16R8, 16 MB Flash, 8 MB OPI PSRAM, display IPS 3.5" 480×320 QSPI AXS15231B + touch capacitivo I²C).
- **Firmware:** PlatformIO / Arduino-ESP32, `Arduino_GFX`, `LVGL 8.3.11`, `ArduinoJson 7`, `WebSocketsServer`, `Preferences` (NVS).
- **Backend Windows & Distribuição:** Python 3.14 portátil assinado (`pycaw`, `websockets`, `psutil`, `comtypes`, `winrt-Windows.Media.Control`, `winrt-Windows.Devices.Radios`), executores silenciosos `wscript.exe`/`pythonw.exe` e empacotamento via **Inno Setup 6** (`MiniDeckSetup.exe`).
