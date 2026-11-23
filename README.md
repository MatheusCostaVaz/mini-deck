# 🎛️ Mini Deck IoT — Versão 2.3 Oficial

Dispositivo físico autônomo com **display touch IPS de 3.5" (480×320)** baseado em **ESP32-S3** (**Guition JC3248W535C** / compatível com a linha 3.5" ESP32-3248S035), desenvolvido para controle em tempo real de **áudio, telemetria de hardware, mídia, atalhos, hub de jogos e configurações do Windows 11** via **Wi-Fi / WebSocket de ultra-baixa latência**.

---

## 🚀 Como Instalar e Rodar no Windows (Definitivo & 1 Clique)

### ✅ Instalação Automática (Recomendado)
1. Certifique-se de que as dependências Python foram instaladas em `backend\.venv` (veja *Setup Inicial* abaixo, se for a primeira vez no PC).
2. Dê **dois cliques** em [`INSTALAR_AUTOSTART.bat`](INSTALAR_AUTOSTART.bat) na raiz do projeto.
3. Pronto! O instalador:
   - Cria automaticamente o atalho `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\MiniDeckBridge.lnk` apontando para [`backend/iniciar_silencioso.vbs`](backend/iniciar_silencioso.vbs).
   - Executa `wifi_bridge.py` via interpretador oficial assinado **`pythonw.exe`** — **100% silencioso** (sem janela preta no console) e **imune a bloqueios do Windows Smart App Control / Defender**.
   - Inicia a bridge imediatamente em segundo plano, sem precisar reiniciar o computador.

### 🗑️ Desinstalação do Autostart
- Para desativar a inicialização automática com o Windows a qualquer momento, basta dar dois cliques em [`DESINSTALAR_AUTOSTART.bat`](DESINSTALAR_AUTOSTART.bat) na raiz do projeto.

### 📝 Configuração Dinâmica de Jogos e Atalhos (`backend/config.json`)
- Novos jogos manuais/não-Steam e atalhos (apps, protocolos `steam://`/`discord://` ou URLs) podem ser adicionados diretamente em [`backend/config.json`](backend/config.json).
- A bridge possui **hot-reload automático**: basta editar e salvar o arquivo para que os novos botões apareçam em segundos na tela do Mini Deck, **sem precisar recompilar ou regravar o ESP32**.

---

## ✨ Funcionalidades da Versão 2.3

### 🎮 1. Hub de Jogos Dinâmico & Game HUD (`screen_games` + `screen_game_hud`)
- **Auto-Scan da Biblioteca Steam:** detecta a Steam pelo Registro do Windows (`HKCU\Software\Valve\Steam`), lê todas as bibliotecas em `libraryfolders.vdf` e importa automaticamente todos os jogos instalados via `appmanifest_*.acf` (filtrando ferramentas internas como Steamworks, Proton e Redistributables).
- **Jogos Personalizados (`config.json`):** mescla jogos manuais (como **WARDOGS**) com os jogos instalados na Steam sem duplicatas.
- **Game HUD Dedicado:** telemetria de CPU/GPU/RAM ao vivo, slider lateral exclusivo para o **volume do jogo ativo**, botão discreto **`<`** no cabeçalho para retornar à seleção de jogos e verificação via `psutil` contra reabertura de jogos já em execução.
- **CS2 GSI C4 Timer (40s — Ultra-Low Latency):** servidor HTTP local em `127.0.0.1:3000` com `buffer: 0.0`, `throttle: 0.0`, `TCP_NODELAY` no WebSocket e `WiFi.setSleep(false)` no ESP32, com alertas visuais críticos (< 10s / < 5s).

### ⚙️ 2. Menu de Configurações Modular (`screen_settings`)
Hub em grade 2×2 com telas dedicadas e espaçosas para uso confortável no touch:
- **📶 Wi-Fi (`screen_wifi_manager`):** Sistema **Multi-Wi-Fi** com até **4 redes salvas na NVS** (`Preferences.h`), auto-seleção no boot pela rede conhecida com melhor sinal (RSSI), scanner assíncrono, teclado virtual LVGL completo em tela cheia (`480×240`) e fallback seguro de 12s para a rede anterior em caso de falha.
- **ᛒ Bluetooth do PC:** controle direto do rádio Bluetooth do Windows via WinRT (`Windows.Devices.Radios`) com feedback instantâneo e sincronização bidirecional.
- **💡 Tela & Brilho (`screen_brightness`):** slider horizontal largo (`10%–100%`) e presets rápidos (`25%`, `60%`, `100%`) com controle por hardware **PWM LEDC** (`GFX_BL` — GPIO 1 na Guition JC3248W535C / configurável para GPIO 27 em variantes ESP32-3248S035) persistido na NVS.
- **ℹ️ Sistema / Info (`screen_system_info`):** versão do firmware, memória livre ao vivo (Heap RAM, PSRAM e Flash), status e IP do PC conectado via WebSocket, seletor de **Standby / Auto-Sleep** (`Desativado`, `2 min`, `5 min`, `10 min`), botão de calibração touch e botão seguro **Reiniciar Mini Deck** (`ESP.restart()`).

### 🌙 3. Descanso de Tela Inteligente (`screen_screensaver`)
- Relógio digital grande (`48 px`), data em português via NTP (`GMT-3`) e **clima em tempo real de São Luís** (Open-Meteo API, atualizado a cada 30 min) com redução suave automática do brilho.
- **Gatilhos:** inatividade real no Windows via `GetLastInputInfo` (> 5 min sem jogo ativo), suspensão/desligamento do PC (queda do WebSocket > 20s) ou tempo configurado em Standby.
- **Wake Up:** toque em qualquer ponto da tela (consumido com segurança sem acionar botões abaixo) ou retorno imediato de input no PC.

### 📊 4. Monitor de Hardware (`screen_hardware`)
- Telemetria em tempo real (1,5s) com uso (`%`) e temperaturas (`°C`) de **CPU** (PDH/WMI) e **GPU NVIDIA** (`nvml.dll`, sem subprocessos), além de **RAM** (GB e `%`) e **Disco C:**, com alerta visual acima de 80°C.

### 🎚️ 5. Mixer de Áudio & Player de Mídia (`screen_audio` + `screen_media`)
- **Mixer por Canais:** controle independente (0–100% + Mute) de **Master**, **Navegador**, **Voz/Discord** e **Jogo Ativo**, com recuperação automática **Hot-Plug WASAPI** ao conectar/desconectar fones Bluetooth ou headsets USB.
- **Player de Mídia:** integração nativa com o **Windows Media Manager (WinRT SMTC)** exibindo título, artista/canal, estado de reprodução, botões **Anterior / Play-Pause / Próximo** e slider dedicado do navegador.
- **Header Universal & Home:** relógio NTP, botão **Home**, status de conexão e botão rápido de **Mute do Microfone** acessíveis em todas as telas.

---

## 📝 Exemplo de `backend/config.json`

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

Nenhuma credencial de rede fica exposta no repositório Git (`firmware/include/secrets.h` está no `.gitignore`).
- Opcionalmente, copie [`firmware/include/secrets.h.example`](firmware/include/secrets.h.example) para `firmware/include/secrets.h` caso queira definir uma rede padrão de fábrica ao compilar.
- Sem `secrets.h`, o Mini Deck inicia normalmente e permite cadastrar até 4 redes pelo teclado virtual em **⚙️ Configurações → Wi-Fi**.

---

## 🛠️ Setup Inicial & Build do Código-Fonte

### 1. Preparar Ambiente Python no Windows
```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```
Em seguida, execute [`INSTALAR_AUTOSTART.bat`](INSTALAR_AUTOSTART.bat) na raiz do projeto.

### 2. Compilar e Gravar o Firmware ESP32-S3
```powershell
pio run -d firmware -t upload
```

### Tecnologias Utilizadas
- **Hardware:** Guition **JC3248W535C** (ESP32-S3-N16R8, 16 MB Flash, 8 MB OPI PSRAM, display IPS 3.5" 480×320 QSPI AXS15231B + touch capacitivo I²C).
- **Firmware:** PlatformIO / Arduino-ESP32, `Arduino_GFX`, `LVGL 8.3.11`, `ArduinoJson 7`, `WebSocketsServer`, `Preferences` (NVS).
- **Backend Windows:** Python 3.14 (`pycaw`, `websockets`, `psutil`, `comtypes`, `winrt-Windows.Media.Control`, `winrt-Windows.Devices.Radios`) + executor silencioso `wscript.exe` / `pythonw.exe`.
