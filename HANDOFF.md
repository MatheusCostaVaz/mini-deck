# Mini Deck IoT — Handoff Técnico (v2.0)

> **Última atualização:** 2026-11-23  
> **Repositório:** https://github.com/MatheusCostaVaz/mini-deck.git  
> **Branch ativa de desenvolvimento:** `v2-dev`  
> **Branch de produção:** `main` (v2.0 oficial mesclada via `--no-ff` no commit `38229d6`)

---

## 1. Visão Geral do Estado Atual

O **Mini Deck IoT v2.0** é um controlador físico autônomo com tela touch de 3,5" (ESP32-S3) operando **100% sem fio via Wi-Fi + WebSockets + Auto-Discovery UDP**, conectado ao Windows 11 por uma bridge silenciosa empacotada em executável único (`backend/dist/MiniDeckBridge.exe`, ~10,8 MB, fora do Git conforme `.gitignore`).

### Módulos entregues e validados em hardware real
- **Dashboard Hub (`screen_home`):** 6 cards minimalistas (ícones Phosphor 42 px) + barra lateral de Volume Master (`GERAL`).
- **Mixer de Áudio (`screen_audio`):** 4 canais independentes (`MASTER`, `EDGE`/navegador, `DISCORD`/chat, `JOGO`) com mute individual e sincronização bidirecional WASAPI.
- **Player de Mídia (`screen_media`):** Now Playing nativo do Windows 11 (WinRT SMTC), controles `prev` / `play_pause` / `next` e slider lateral dedicado ao navegador (`browser`).
- **Monitor de Hardware (`screen_hardware`):** Telemetria em tempo real (1,5 s) de **CPU (% + °C)**, **GPU NVIDIA (% + °C)**, **RAM (% + GB usados/total)** e **Disco C:** com alertas visuais dinâmicos de carga e temperatura (`> 80 °C`).
- **Atalhos / Macros (`screen_macros`):** Grade 2×3 com `EDGE`, `STEAM`, `DISCORD`, `DISNEY+`, `MAX` e entrada para o **Hub de Jogos (`JOGOS`)**.
- **Hub de Jogos (`screen_games`) + Game HUD (`screen_game_hud`):**
  - Seletor 2×2 (`CS2`, `PUBG`, `WATCH DOGS`, `OUTROS`) com verificação via `psutil` para **não reabrir** jogos já em execução.
  - Painel Game HUD unificado: 3 tiles superiores de hardware (CPU/GPU/RAM), slider lateral esquerdo exclusivo para o **Volume do Jogo ativo (`target: "game"`)** e bloco inferior dinâmico (**CS2 C4 Timer 40 s Ultra-Low Latency** no modo CS2, ou resumo de sessão/RAM/Disco nos demais jogos).

---

## 2. Arquitetura Atual da v2.0

### 2.1 Hardware e Firmware (`firmware/`)

| Componente | Especificação / Configuração |
|---|---|
| **Placa** | Guition **JC3248W535C** (`esp32-s3-devkitc-1`, ESP32-S3-N16R8, 16 MB QIO Flash, 8 MB OPI PSRAM, porta `COM8`) |
| **Display** | AXS15231B 3,5" 320×480 QSPI → `Arduino_GFX @ 1.5.5` + `Arduino_Canvas` em PSRAM (paisagem `480×320`, `rotation = 1`) |
| **Touch** | AXS15231B capacitivo I2C (`SDA=4, SCL=8, ADDR=0x3B`) |
| **UI Engine** | `lvgl @ ~8.3.11` (`LV_MEM_SIZE = 96 KB` em SRAM; framebuffer em PSRAM) |
| **Rede** | `WiFi.begin(...)` assíncrono + `WiFi.setSleep(false)`, NTP GMT-3 (`a.st1.ntp.br`), `WebSocketsServer(81)`, UDP Broadcast na porta `4210` a cada 2 s (`{"device":"minideck","port":81}`) |
| **Fontes/Ícones** | `firmware/src/assets/`: `phosphor_20/28/32/42.c`, `inter_16/medium_16/bold_16.c` (Latin-1 completo, inclui acentos PT-BR e `°`) |
| **Uso de Memória** | RAM ~`45,1%` (`147,8 KB / 327 KB`) · Flash ~`27,3%` (`1,79 MB / 6,55 MB`) |

#### Mapa das 7 Telas LVGL (`firmware/src/ui.h` e `firmware/src/ui.cpp`)
Todas as telas compartilham o **Header Universal de 58 px** (`[ HORA NTP ] [ HOME 62×48 ] --- CENTRO DINÂMICO --- [ MIC 90×48 ]`) e, exceto o Mixer, a **Sidebar Esquerda de 70×246 px**:

| `UiScreenId` | Tela | Sidebar Esquerda | Conteúdo Central |
|---|---|---|---|
| `0` | `UI_SCREEN_HOME` | `GERAL` (`master`) | Grade 2×3 de módulos (`Mídia`, `Mixer`, `Brilho`, `Rede`, `Atalhos`, `Hardware`) |
| `1` | `UI_SCREEN_AUDIO` | *(4 colunas full-width)* | Sliders verticais `MASTER`, `EDGE`, `DISCORD`, `JOGO` + mute |
| `2` | `UI_SCREEN_MEDIA` | `browser` (`EDGE`) | Card Now Playing (scroll circular) + botões `Prev / Play-Pause / Next` |
| `3` | `UI_SCREEN_HARDWARE` | `master` | 3 cards horizontais (`CPU % + °C + Disco C:`, `GPU % + °C`, `RAM % + GB`) |
| `4` | `UI_SCREEN_MACROS` | `master` | Grade 2×3 (`EDGE`, `STEAM`, `DISCORD`, `DISNEY+`, `MAX`, `JOGOS`) |
| `5` | `UI_SCREEN_GAMES` | `master` | Grade 2×2 grande (`CS2`, `PUBG`, `WATCH DOGS`, `OUTROS`) |
| `6` | `UI_SCREEN_GAME_HUD` | `JOGO` (`game`) | Topo: 3 tiles `CPU / GPU / RAM`; Base: **CS2 C4 Timer (40 s)** ou **Sessão/RAM/Disco** |

---

### 2.2 Backend Windows (`backend/`)

- **`backend/wifi_bridge.py`:** Cliente WebSocket `asyncio` + Auto-Discovery UDP (`4210`) + mutex de instância única + log rotativo (`wifi_bridge.log`, 512 KB) + servidor HTTP local CS2 GSI (`127.0.0.1:3000`).
- **`backend/audio_controller.py`:** Wrapper WASAPI (`pycaw` + `comtypes`) para Master, Microfone e sessões por aplicativo, com recuperação automática de **Hot-Plug** (`_with_device_retry` + `refresh_devices()`).
- **`backend/media_controller.py`:** Leitura Now Playing e controle multimídia via **WinRT SMTC** (`winrt-Windows.Media.Control`) com fallback `VK_MEDIA_*` (`user32.keybd_event`) e sanitização Latin-1.
- **`backend/gamestate_integration_minideck.cfg`:** Configuração GSI oficial do CS2 (`buffer "0.0"`, `throttle "0.0"`, `timeout "2.0"`), também instalada em:
  `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\cfg\gamestate_integration_minideck.cfg`
- **`backend/MiniDeckBridge.spec`:** Spec do PyInstaller (`--onefile --noconsole`) com hidden imports explícitos (`comtypes`, `pycaw`, `websockets.legacy`, `psutil`, `winrt.*`).

#### Protocolo JSON Bidirecional (WebSocket porta `81` / Serial fallback)
- **ESP32 → Windows:**
  - `{"event":"set_vol","target":"master|browser|discord|game","val":0..100}`
  - `{"event":"toggle_mute","target":"master|browser|discord|game|mic"}`
  - `{"cmd":"media","action":"play_pause|next|prev"}`
  - `{"cmd":"macro","action":"edge|steam|discord|disney|hbo"}`
  - `{"cmd":"launch_game","game":"cs2|pubg|watchdogs|custom"}`
  - `{"event":"screen","screen":"home|audio|media|hardware|macros|games|game_hud"}`
  - `{"event":"ready",...}` / `{"event":"pong"}`
- **Windows → ESP32:**
  - `{"sync":"vol","target":"...","val":0..100}` / `{"sync":"mute",...}` / `{"sync":"label",...}`
  - `{"sync":"media","title":"...","artist":"...","playing":true|false}`
  - `{"sync":"stats","cpu":31,"cpu_temp":53,"gpu":14,"gpu_temp":47,"ram_pct":30,"ram_used":"9.7","ram_total":"31.9","disk_pct":66}`
  - `{"sync":"cs2_c4","state":"planted|idle","time_left":0..40}`
  - `{"cmd":"ping"}`

---

## 3. Últimas Decisões Técnicas e Armadilhas (Gotchas)

1. **`Arduino_GFX` fixado em `@ 1.5.5`:** Versões `1.6.x` exigem Arduino-ESP32 3.x (`esp32-hal-periman.h`) e quebram o build no core `espressif32 @ 7.0.1` do PlatformIO. Não atualizar sem migrar a plataforma inteira.
2. **Ordem de inicialização AXS15231B:** O barramento QSPI do display (`gfx->begin()`) **deve** inicializar antes do I2C do touch (`Wire.begin(4, 8)`).
3. **C++11 estrito no PlatformIO:** Structs com inicializadores padrão precisam de construtores explícitos para inicialização com chaves.
4. **Símbolo de grau `°` (`U+00B0`):** As fontes nativas `lv_font_montserrat_*` do LVGL 8.3 não possuem `°`. Temperaturas usam `inter_bold_16` (Latin-1) com `"\xC2\xB0" "C"`.
5. **Assets de imagem LVGL 9 (`disney.c`, `hbo.c`, `netflix.c`):** Apesar de compilarem com guarda `#if LVGL_VERSION_MAJOR >= 9`, seus arrays usam formato planar `RGB565A8` (LVGL 9), incompatível visualmente com `LV_IMG_CF_TRUE_COLOR_ALPHA` (LVGL 8). Por isso os cards de `screen_macros` usam ícones vetoriais **Phosphor** (`phosphor_32`).
6. **Python 3.14 e `winrt`:** Usar sempre `backend\.venv\Scripts\python.exe` (o alias `python` do WindowsApps falha em sandbox). O pacote `winsdk` foi substituído pelas wheels nativas `winrt-Windows.Media.Control` para Python 3.14.
7. **Telemetria térmica sem privilégios de Admin e sem subprocessos:**
   - **GPU:** `nvml.dll` via `ctypes` (`nvmlDeviceGetUtilizationRates` + `nvmlDeviceGetTemperature`), `< 1 ms`, com fallback `nvidia-smi` assíncrono (`CREATE_NO_WINDOW`) e Windows PDH (`\GPU Engine(*)\Utilization Percentage`).
   - **CPU Temp:** `pdh.dll` via `ctypes` (`\Thermal Zone Information(*)\Temperature`, `~2 ms`, sem precisar de Admin — diferente de `MSAcpi_ThermalZoneTemperature`), selecionando a maior zona térmica válida (`10..125 °C`).
8. **Pipeline CS2 GSI Ultra-Low Latency:**
   - `gamestate_integration_minideck.cfg` com `buffer "0.0"` e `throttle "0.0"`.
   - `WiFi.setSleep(false)` no ESP32-S3 (evita 100–300 ms de DTIM sleep).
   - `TCP_NODELAY` nos sockets do WebSocket e do servidor HTTP GSI (`127.0.0.1:3000`).
   - Suporte a `Expect: 100-continue` e `Transfer-Encoding: chunked` (usados pelo `libcurl` do CS2 em payloads > 1 KB) e disparo imediato do frame `40s` na mesma corrotina.
9. **Hot-Plug de Áudio WASAPI:** Trocar ou desconectar fones Bluetooth (ex.: `AZ09`) lançava `_ctypes.COMError (-2147023279)`. Resolvido com `_with_device_retry` em `audio_controller.py` e `try/except (COMError, Exception)` + `refresh_devices()` em `wifi_bridge.py`. Todas as chamadas COM devem permanecer na mesma thread do loop `asyncio`.
10. **Prioridade do canal `"game"` e detecção de jogo ativo:**
    - `_pick_game_session` prioriza jogos conhecidos (`cs2.exe`, `TslGame.exe`, `WatchDogs*.exe`, `WardogsClient-Win64-Shipping.exe`) sobre launchers (`steam.exe`, `epicgameslauncher.exe`).
    - `_launch_game` verifica `psutil.process_iter(["name"])` antes de acionar `steam://` para nunca tentar reabrir um jogo já aberto ao retornar para a `screen_game_hud`.
    - No PC atual, o botão `"WATCH DOGS"` detecta automaticamente **WARDOGS** (`steam://rungameid/1867240`) caso não haja atalho de Watch Dogs no Desktop.
11. **PyInstaller no Windows:** Sempre encerrar `MiniDeckBridge.exe` antes de recompilar para evitar lock no `.exe`, e usar `backend\MiniDeckBridge.spec` (sem `--collect-submodules`, que trava no Python 3.14).

---

## 4. Comandos Rápidos de Build e Operação

```powershell
# Compilar e gravar firmware no ESP32-S3 (COM8)
& "C:\Users\mathe\AppData\Local\Python\pythoncore-3.14-64\Scripts\pio.exe" run -d firmware -t upload

# Encerrar bridge atual e recompilar executável autônomo
Stop-Process -Name MiniDeckBridge -Force -ErrorAction SilentlyContinue
backend\.venv\Scripts\pyinstaller.exe --clean --noconfirm --distpath backend\dist --workpath backend\build backend\MiniDeckBridge.spec

# Iniciar executável desacoplado em segundo plano
Start-Process -FilePath backend\dist\MiniDeckBridge.exe

# Testar CS2 GSI localmente (porta 3000)
Invoke-RestMethod -Uri "http://127.0.0.1:3000" -Method Post -ContentType "application/json" -Body '{"round":{"bomb":"planted"},"bomb":{"state":"planted"}}'
```

---

## 5. Próximos Passos Pendentes (Roadmap v2.1+)

1. **Card 3 da Home (`Brilho` / Iluminação — atualmente `"Em breve"`):**
   - Implementar `screen_brightness` com controle de brilho da própria tela do Mini Deck (PWM no pino `GFX_BL`) e/ou brilho do monitor Windows (DDC/CI / WMI) ou automação Smart Home (Tuya/Home Assistant/MQTT).
2. **Card 4 da Home (`Rede` — atualmente exibe apenas toast com IP):**
   - Implementar `screen_network` exibindo SSID, IP local, força do sinal Wi-Fi (RSSI), status do WebSocket/CS2 GSI, botão de reconexão e telemetria básica de rede do PC (upload/download via `psutil.net_io_counters`).
3. **Documentação e Release Oficial v2.0 no GitHub:**
   - Atualizar `README.md` com capturas/descrição completa da v2.0 (7 telas, Media SMTC, Hardware + °C, Hub Gamer + CS2 GSI).
   - Criar tag `v2.0` na `main` e publicar a Release no GitHub anexando `backend/dist/MiniDeckBridge.exe` e `gamestate_integration_minideck.cfg`.
4. **Refinamentos de UX / Configurabilidade:**
   - Avaliar renomear o card `"WATCH DOGS"` para `"WARDOGS"` (ou tornar a lista de macros/jogos configurável via arquivo JSON externo em `backend/config.json`).
   - Adicionar botão discreto de voltar para `screen_games` dentro de `screen_game_hud` (hoje retorna à Home via `[ HOME ]`).
   - Mover credenciais Wi-Fi (`WIFI_SSID` / `WIFI_PASS` em `firmware/src/main.cpp`) para `secrets.h` ignorado pelo Git ou portal cativo (WiFiManager/NVS) antes de tornar o repositório público.
