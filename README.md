# 🎛️ Mini Deck IoT — Versão 2.3 Oficial

Dispositivo físico autônomo com **display touch IPS de 3.5" (480×320)** baseado em **ESP32-S3** (**Guition JC3248W535C** / compatível com a linha 3.5" ESP32-3248S035), projetado para controle em tempo real de **áudio, mídia, telemetria de hardware, atalhos, hub de jogos e configurações do Windows 11** via **Wi-Fi / WebSocket de ultra-baixa latência**.

---

## ✨ Telas e Funcionalidades (v2.3)

### 🏠 1. Dashboard / Home (`screen_home`)
- **Header Universal** em todas as telas com relógio digital sincronizado via NTP (`GMT-3`), botão **Home**, indicador de status da conexão e botão rápido de **Mute do Microfone** (verde/vermelho).
- **Slider Master lateral esquerdo** persistente para ajuste instantâneo do volume geral.
- **Grade modular de acesso rápido:** Mídia, Mixer, Brilho, Configurações, Atalhos/Jogos e Monitor de Hardware.

### 🎚️ 2. Mixer de Áudio (`screen_audio`)
- Controle bidirecional independente (0–100% + Mute) para **Master**, **Navegador** (Edge/Chrome/etc.), **Voz/Chat** (Discord/Teams) e **Jogo Ativo**.
- **Proteção Hot-Plug WASAPI:** recupera automaticamente sessões de áudio caso fones Bluetooth, headsets USB ou saídas padrão sejam desconectados ou trocados em tempo real.

### 🎵 3. Player de Mídia (`screen_media`)
- Integração nativa com o **Windows Media Session Manager (WinRT SMTC)** exibindo faixa/vídeo, artista/canal e estado de reprodução.
- Controles touch dedicados (**Anterior**, **Play/Pause**, **Próximo**) acompanhados do slider lateral vinculado diretamente ao volume do navegador.

### 📊 4. Monitor de Hardware (`screen_hardware`)
- Telemetria em tempo real (atualização a cada 1,5s) com uso (`%`) e temperaturas (`°C`) de:
  - **CPU:** utilização (`psutil`) + temperatura via Windows PDH / WMI.
  - **GPU (NVIDIA):** utilização e temperatura via **NVML (`nvml.dll`)** sem subprocessos (com fallback PDH).
  - **RAM e Disco C:** memória utilizada em GB/porcentagem e ocupação do disco principal.
- Destaque automático de alerta térmico quando CPU ou GPU ultrapassam 80°C.

### 🎮 5. Hub de Jogos & Game HUD (`screen_games` + `screen_game_hud`)
- **Auto-Scan da Biblioteca Steam:** localiza a Steam pelo Registro do Windows (`HKCU\Software\Valve\Steam`), lê todas as bibliotecas em `libraryfolders.vdf` e extrai automaticamente os jogos instalados via `appmanifest_*.acf` (ignorando ferramentas internas como Steamworks/Proton/Redistributables).
- **Suporte a Jogos e Atalhos Personalizados (`backend/config.json`):** hot-reload automático ao salvar o JSON — sem precisar recompilar o firmware C++.
- **Game HUD Dedicado:** exibe CPU/GPU/RAM ao vivo, slider lateral exclusivo para o **Volume do Jogo Ativo**, botão discreto **`<`** no cabeçalho para trocar de jogo rapidamente e proteção via `psutil` contra reabertura de jogos já em execução.
- **CS2 GSI C4 Timer (Ultra-Low Latency):** servidor HTTP local na porta `3000` com `buffer: 0.0`, `throttle: 0.0`, `TCP_NODELAY` no WebSocket e `WiFi.setSleep(false)` no ESP32, exibindo contagem regressiva de 40s com alertas visuais críticos (< 10s / < 5s).

### ⚙️ 6. Central de Configurações (`screen_settings`)
Hub limpo em grade 2×2 separando cada recurso em telas dedicadas e confortáveis para toque:
- **📶 Wi-Fi (`screen_wifi_manager`):** Sistema **Multi-Wi-Fi** com até **4 redes salvas na memória NVS** (`Preferences.h`), auto-seleção no boot pela rede conhecida de melhor sinal (RSSI), scanner assíncrono, teclado virtual LVGL completo em tela cheia e fallback seguro de 12 segundos para a rede anterior em caso de falha.
- **ᛒ Bluetooth do PC:** controle nativo do rádio Bluetooth do Windows via WinRT (`Windows.Devices.Radios`) com toggle direto e sincronização de estado.
- **💡 Tela & Brilho (`screen_brightness`):** slider horizontal largo (10%–100%) e presets rápidos (`25%`, `60%`, `100%`) com controle via hardware **PWM LEDC** (`GFX_BL` — GPIO 1 na Guition JC3248W535C / configurável para GPIO 27 em placas ESP32-3248S035) persistido na NVS.
- **ℹ️ Sistema / Info (`screen_system_info`):** versão do firmware, memória livre ao vivo (Heap RAM, PSRAM e Flash), status da conexão Host/IP do PC, seletor de **Standby / Auto-Sleep** (`Desativado`, `2 min`, `5 min`, `10 min`), botão de calibração touch e botão seguro **Reiniciar Mini Deck** (`ESP.restart()`).

### 🌙 7. Descanso de Tela Inteligente (`screen_screensaver`)
- Relógio digital grande centralizado (`48 px`), data em português sincronizada via NTP e **clima em tempo real de São Luís** (Open-Meteo API, atualização a cada 30 min) com redução suave automática do brilho.
- **Ativação automática:** inatividade real no PC via API do Windows (`GetLastInputInfo` > 5 min sem jogo ativo), perda do WebSocket > 20s (PC suspenso/desligado) ou tempo configurado em Standby.
- **Acordar (Wake Up):** toque em qualquer ponto da tela (consumido com segurança sem acionar botões abaixo) ou retorno imediato de input no Windows.

---

## 🚀 Como Iniciar no Windows

1. Ligue o Mini Deck na alimentação — ele escaneia e conecta automaticamente na rede Wi-Fi conhecida com melhor sinal (ou aguarda configuração pelo menu **⚙️ Configurações → Wi-Fi**).
2. Certifique-se de que o computador está na mesma rede local (a descoberta automática ocorre via **UDP porta `4210`** e conexão **WebSocket porta `81`**).
3. Inicie a bridge do Windows de uma das duas formas:

### Opção A — Via Python (Recomendado)
```powershell
# Em primeiro plano (com saída no terminal)
backend\.venv\Scripts\python.exe backend\wifi_bridge.py

# Em segundo plano silencioso (sem janela de console)
backend\.venv\Scripts\pythonw.exe backend\wifi_bridge.py
```

### Opção B — Via Executável Autônomo (`.exe`)
```powershell
backend\dist\MiniDeckBridge.exe
```
> **Nota (Windows Smart App Control):** caso o Windows bloqueie executáveis locais sem assinatura digital, utilize a **Opção A** (`pythonw.exe` oficial assinado) ou configure o início automático via `python backend/setup_startup.py`.

---

## 📝 Configurando Jogos e Atalhos (`backend/config.json`)

Edite o arquivo [`backend/config.json`](backend/config.json) com qualquer editor de texto. A bridge detecta alterações automaticamente (**hot-reload**) e atualiza os botões na tela do Mini Deck em segundos — sem recompilar o firmware:

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

- Jogos instalados na **Steam** são adicionados automaticamente após os jogos personalizados, sem duplicar itens já cadastrados.
- `target` em `macros` aceita executáveis (`msedge.exe`), protocolos (`steam://`, `discord://`) ou URLs (`https://...`).

---

## 🔐 Credenciais Wi-Fi Locais (`secrets.h`)

Nenhuma senha de rede fica exposta no repositório Git (`firmware/include/secrets.h` está listado no `.gitignore`).
- Para configurar uma rede padrão de fábrica opcional ao compilar o firmware, copie [`firmware/include/secrets.h.example`](firmware/include/secrets.h.example) para `firmware/include/secrets.h` e preencha `DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASS`.
- Caso contrário, basta compilar normalmente e cadastrar até 4 redes diretamente pela tela touch em **⚙️ Configurações → Wi-Fi**.

---

## 🛠️ Arquitetura e Build do Código-Fonte

### Tecnologias
- **Hardware:** Guition **JC3248W535C** (ESP32-S3-N16R8, 16 MB Flash, 8 MB OPI PSRAM, display IPS 3.5" 480×320 QSPI AXS15231B + touch capacitivo I²C).
- **Firmware:** PlatformIO / Arduino-ESP32, `Arduino_GFX`, `LVGL 8.3.11`, `ArduinoJson 7`, `WebSocketsServer`, `Preferences` (NVS).
- **Backend Windows:** Python 3.14 (`pycaw`, `websockets`, `psutil`, `comtypes`, `winrt-Windows.Media.Control`, `winrt-Windows.Devices.Radios`) + PyInstaller.

### Instalar Dependências e Recompilar o Backend
```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt

# Opcional: empacotar executável único (backend/dist/MiniDeckBridge.exe)
.\.venv\Scripts\pyinstaller.exe --clean --noconfirm --distpath backend\dist --workpath backend\build backend\MiniDeckBridge.spec
```

### Compilar e Gravar o Firmware ESP32-S3
```powershell
pio run -d firmware -t upload
```
