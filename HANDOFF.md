# Mini Deck IoT — Handoff Técnico (v2.3)

> **Última atualização:** 2026-11-23  
> **Repositório:** https://github.com/MatheusCostaVaz/mini-deck.git  
> **Branch ativa de desenvolvimento:** `v2-dev`  
> **Branch de produção:** `main` (v2.3 oficial mesclada via `--no-ff` no commit `69ea230`, tag anotada `v2.3`)

---

## 1. Visão Geral do Estado Atual

O **Mini Deck IoT v2.3** é um controlador físico autônomo com tela touch IPS de 3,5" (ESP32-S3) operando **100% sem fio via Wi-Fi + WebSockets + Auto-Discovery UDP**, distribuído no Windows 10/11 por meio do **instalador único oficial `dist/MiniDeckSetup.exe` (~12,5 MB)** gerado pelo **Inno Setup 6** com runtime Python 3.14.6 portátil oficialmente assinado pela *Python Software Foundation* (sem exigir Python pré-instalado, sem elevação UAC, sem janelas pretas de console e 100% imune a bloqueios do Windows Smart App Control).

### Módulos entregues e validados em hardware real
- **Dashboard Hub (`screen_home`):** Cards modulares com ícones Phosphor + barra lateral de Volume Master (`GERAL`).
- **Mixer de Áudio (`screen_audio`):** 4 canais independentes (`MASTER`, `EDGE`/navegador, `DISCORD`/chat, `JOGO`) com mute individual, sincronização bidirecional WASAPI e recuperação automática **Hot-Plug** para headsets Bluetooth/USB.
- **Player de Mídia (`screen_media`):** Now Playing nativo do Windows 11 (WinRT SMTC), controles `prev` / `play_pause` / `next` e slider lateral dedicado ao navegador (`browser`).
- **Monitor de Hardware Contínuo (`screen_hardware`):** Telemetria em tempo real (1,5 s) de **CPU (% + °C)**, **GPU NVIDIA (% + °C)**, **RAM (% + GB usados/total)** e **Disco C:** com alertas visuais (`> 80 °C`) e **bloqueio Anti-Standby**.
- **Atalhos Dinâmicos (`screen_macros`):** Grade alimentada dinamicamente por `backend/config.json` com **hot-reload automático** (apps locais, protocolos `steam://`/`discord://` e URLs) + entrada para o Hub de Jogos.
- **Hub de Jogos Dinâmico (`screen_games`) + Game HUD (`screen_game_hud`):**
  - **Steam Auto-Scan:** lê `HKCU\Software\Valve\Steam`, percorre todas as bibliotecas em `libraryfolders.vdf` e importa todos os jogos via `appmanifest_*.acf` (filtrando ferramentas internas como Steamworks/Proton/Redistributables), mesclando sem duplicatas com jogos manuais de `config.json` (ex.: `WARDOGS`).
  - Proteção via `psutil` contra reabertura de jogos já em execução.
  - Painel Game HUD unificado com 3 tiles de hardware (CPU/GPU/RAM), slider lateral exclusivo do **Volume do Jogo (`target: "game"`)**, botão discreto **`<`** no cabeçalho para retornar direto a `screen_games`, **CS2 C4 Timer 40 s Ultra-Low Latency** e **bloqueio Anti-Standby**.
- **Central de Configurações Modular (`screen_settings`):**
  - **Wi-Fi (`screen_wifi_manager`):** Sistema **Multi-Wi-Fi** com até 4 redes salvas na NVS (`Preferences.h`), auto-seleção no boot pelo melhor RSSI, scanner assíncrono, teclado virtual LVGL completo (`480×240`) e fallback seguro de 12 s.
  - **Bluetooth do PC:** toggle bidirecional do rádio Bluetooth via WinRT (`Windows.Devices.Radios`).
  - **Tela & Brilho (`screen_brightness`):** controle por hardware **PWM LEDC** (`GFX_BL` = GPIO 1), slider `10%–100%` + presets (`25%`, `60%`, `100%`) persistidos na NVS.
  - **Sistema / Info (`screen_system_info`):** versão do firmware, Heap RAM / PSRAM / Flash livres ao vivo, IP do PC conectado, seletor de **Standby / Auto-Sleep** (`Desativado`, `2 min`, `5 min`, `10 min`), calibração touch e `ESP.restart()`.
- **Descanso de Tela Inteligente (`screen_screensaver`):**
  - Relógio digital grande (`48 px`), data PT-BR via NTP (`GMT-3`) e **clima de São Luís** via Open-Meteo API (atualização a cada 30 min) com dimerização suave automática.
  - Disparado por inatividade real no Windows (`GetLastInputInfo` > 5 min sem jogo ativo), queda do WebSocket (> 20 s) ou temporizador local de standby — sempre respeitando o **Anti-Standby** quando em `screen_hardware`, `screen_games` ou `screen_game_hud`.

---

## 2. Arquitetura Atual da v2.3

### 2.1 Hardware e Firmware (`firmware/`)

| Componente | Especificação / Configuração |
|---|---|
| **Placa** | Guition **JC3248W535C** (`esp32-s3-devkitc-1`, ESP32-S3-N16R8, 16 MB QIO Flash, 8 MB OPI PSRAM, porta `COM8`) |
| **Display & Backlight** | AXS15231B 3,5" 320×480 QSPI → `Arduino_GFX @ 1.5.5` + `Arduino_Canvas` em PSRAM (paisagem `480×320`, `rotation = 1`); Backlight PWM LEDC em `GFX_BL = GPIO 1` |
| **Touch** | AXS15231B capacitivo I²C (`SDA=4, SCL=8, INT=3, ADDR=0x3B`) |
| **UI Engine** | `lvgl @ ~8.3.11` (`LV_MEM_SIZE = 96 KB` em SRAM; framebuffer em PSRAM; `LV_FONT_MONTSERRAT_48` habilitada para o relógio do screensaver) |
| **Rede & Credenciais** | `firmware/src/wifi_manager.h/.cpp` (NVS `Preferences` até 4 redes + auto-seleção por RSSI + fallback `#if __has_include("secrets.h")` ignorado no Git); `WiFi.setSleep(false)`, NTP GMT-3, `WebSocketsServer(81)`, UDP Broadcast porta `4210` |
| **Fontes/Ícones** | `firmware/src/assets/`: `phosphor_20/28/32/42.c`, `inter_16/medium_16/bold_16.c` (Latin-1 completo, inclui acentos PT-BR e `°`) |
| **Uso de Memória (v2.3)** | RAM `60,8%` (`199.376 / 327.680 bytes`) · Flash `29,8%` (`1.950.781 / 6.553.600 bytes`) |

#### Mapa das 12 Telas LVGL (`firmware/src/ui.h` e `firmware/src/ui.cpp`)
Todas as telas interativas compartilham o **Header Universal de 58 px** (`[ HORA NTP ] [ HOME ] [< opcional em Game HUD] --- CENTRO DINÂMICO --- [ MIC ]`):

| `UiScreenId` | Tela | Sidebar Esquerda | Conteúdo Central / Função |
|---|---|---|---|
| `0` | `UI_SCREEN_HOME` | `GERAL` (`master`) | Grade modular de cards (`Mídia`, `Mixer`, `Hardware`, `Atalhos`, `Jogos`, `Configurações`) |
| `1` | `UI_SCREEN_AUDIO` | *(4 colunas full-width)* | Sliders verticais `MASTER`, `EDGE`, `DISCORD`, `JOGO` + mute |
| `2` | `UI_SCREEN_MEDIA` | `browser` (`EDGE`) | Card Now Playing (scroll circular) + botões `Prev / Play-Pause / Next` |
| `3` | `UI_SCREEN_HARDWARE` | `master` | 3 cards horizontais (`CPU % + °C + Disco C:`, `GPU % + °C`, `RAM % + GB`) — **Anti-Standby** |
| `4` | `UI_SCREEN_MACROS` | `master` | Grade dinâmica de atalhos (`config.json`) + acesso ao Hub de Jogos |
| `5` | `UI_SCREEN_GAMES` | `master` | Grade 2×N rolável dinâmica (Steam Auto-Scan + `config.json`) — **Anti-Standby** |
| `6` | `UI_SCREEN_GAME_HUD` | `JOGO` (`game`) | Botão `<` no header + 3 tiles `CPU/GPU/RAM` + **CS2 C4 Timer (40 s)** / Sessão — **Anti-Standby** |
| `7` | `UI_SCREEN_SETTINGS` | `master` | Hub 2×2: `Wi-Fi`, `Bluetooth PC`, `Tela & Brilho`, `Sistema / Info` |
| `8` | `UI_SCREEN_WIFI` | *(full-width)* | Scanner Wi-Fi, lista de redes salvas/encontradas e modal com teclado virtual `480×240` |
| `9` | `UI_SCREEN_BRIGHTNESS` | *(full-width)* | Slider PWM `10%–100%` + presets `25%`, `60%`, `100%` (salvos na NVS) |
| `10` | `UI_SCREEN_SYSTEM` | *(full-width)* | Versão, Heap/PSRAM/Flash, IP do PC, seletor de Standby (`0/2/5/10 min`), calibração e Reiniciar |
| `11` | `UI_SCREEN_SCREENSAVER` | *(full-screen dimmed)* | Relógio `48 px`, data PT-BR e clima de São Luís (`Open-Meteo`); toque acorda com segurança |

---

### 2.2 Backend Windows & Empacotamento (`backend/` + raiz)

- **`backend/wifi_bridge.py`:** Cliente WebSocket `asyncio` + Auto-Discovery UDP (`4210`) + log rotativo (`wifi_bridge.log`) + servidor HTTP local CS2 GSI (`127.0.0.1:3000`) + Steam Auto-Scan + hot-reload de `config.json` + Open-Meteo (30 min) + detecção de inatividade (`GetLastInputInfo`) + controle Bluetooth WinRT.
- **`backend/audio_controller.py`:** Wrapper WASAPI (`pycaw` + `comtypes`) com recuperação automática **Hot-Plug** (`_with_device_retry` + `refresh_devices()`).
- **`backend/media_controller.py`:** Integração nativa WinRT SMTC (`winrt-Windows.Media.Control`) com fallback `VK_MEDIA_*` e sanitização Latin-1.
- **`backend/config.json`:** Catálogo dinâmico de jogos manuais (`cs2`, `pubg`, `wardogs`) e atalhos (`edge`, `steam`, `discord`, `disney`, `max`) com hot-reload por `mtime`.
- **`backend/gamestate_integration_minideck.cfg`:** Configuração CS2 GSI (`buffer "0.0"`, `throttle "0.0"`, `timeout "2.0"`).
- **`backend/iniciar_silencioso.vbs`:** Executor silencioso (zero console, proteção contra instâncias duplicadas via WMI); usa automaticamente `{app}\python\pythonw.exe` (instalado) com fallback para `backend\.venv\Scripts\pythonw.exe` (dev local).
- **`backend/parar_bridge.vbs`:** Encerra silenciosamente via WMI apenas processos `python.exe`/`pythonw.exe` cujo `CommandLine` contenha `wifi_bridge.py`.
- **`backend/prepare_portable_runtime.ps1`:** Monta `backend/runtime_portable/` (ignorado pelo Git) baixando o pacote oficial assinado `python-3.14.6-embed-amd64.zip`, configurando `python314._pth`, copiando dependências de execução limpas, validando assinatura digital e executando smoke-test real de imports.
- **`backend/installer.iss` + `build_installer.bat`:** Esteira oficial de compilação **Inno Setup 6** que gera **`dist/MiniDeckSetup.exe`** (`PrivilegesRequired=lowest`, `{localappdata}\MiniDeck`, `{userstartup}\MiniDeckBridge.lnk`, Menu Iniciar, atalhos opcionais na Área de Trabalho e limpeza no desinstalador).

#### Protocolo JSON Bidirecional (WebSocket porta `81`)
- **ESP32 → Windows:**
  - `{"event":"set_vol","target":"master|browser|discord|game","val":0..100}`
  - `{"event":"toggle_mute","target":"master|browser|discord|game|mic"}`
  - `{"cmd":"media","action":"play_pause|next|prev"}`
  - `{"cmd":"macro","action":"<id>"}` / `{"cmd":"launch_game","game":"<id>"}`
  - `{"cmd":"toggle_bt"}`
  - `{"event":"screen","screen":"home|audio|media|hardware|macros|games|game_hud|settings|wifi|brightness|system"}`
  - `{"event":"standby","minutes":0|2|5|10,"saved":true}` / `{"event":"ready",...}` / `{"event":"pong"}`
- **Windows → ESP32:**
  - `{"sync":"vol","target":"...","val":0..100}` / `{"sync":"mute",...}` / `{"sync":"label",...}`
  - `{"sync":"media","title":"...","artist":"...","playing":true|false}`
  - `{"sync":"stats","cpu":31,"cpu_temp":53,"gpu":14,"gpu_temp":47,"ram_pct":30,"ram_used":"9.7","ram_total":"31.9","disk_pct":66}`
  - `{"sync":"cs2_c4","state":"planted|idle","time_left":0..40}`
  - `{"sync":"games_config","games":[...]}` / `{"sync":"macros_config","macros":[...]}`
  - `{"sync":"pc_bt","enabled":true|false}`
  - `{"sync":"weather","temp":28,"city":"São Luís"}`
  - `{"sync":"pc_idle","state":true|false}`
  - `{"cmd":"ping"}`

---

## 3. Decisões Técnicas Críticas e Armadilhas (Gotchas)

1. **Smart App Control vs PyInstaller (Resolvido definitivamente na v2.3):** Executáveis não assinados gerados por PyInstaller (`MiniDeckBridge.exe`) são bloqueados pelo Windows Smart App Control. A solução definitiva adotada é empacotar o **Python Embeddable Package oficial** (assinado pela *Python Software Foundation*) junto com os scripts `.py` no **Inno Setup 6 (`MiniDeckSetup.exe`)**, executando via `wscript.exe` → `iniciar_silencioso.vbs` → `pythonw.exe`.
2. **Isolamento de paths no Python Embeddable (`python314._pth`):** O modo embeddable ignora `PYTHONPATH` e não adiciona o diretório pai do script ao `sys.path` automaticamente. O arquivo `python314._pth` **deve** conter `..` e `Lib\site-packages` + `import site` para que `{app}\python\pythonw.exe` encontre `audio_controller.py` e `media_controller.py` em `{app}\`.
3. **Extração headless no PowerShell:** `Expand-Archive` invoca `Write-Progress` e falha com Win32 `0x5` em consoles redirecionados/headless. `prepare_portable_runtime.ps1` usa `[System.IO.Compression.ZipFile]::ExtractToDirectory` + `$ProgressPreference = "SilentlyContinue"`.
4. **Encoding UTF-8 BOM no Inno Setup 6:** `backend/installer.iss` deve manter **UTF-8 com BOM** para renderizar acentos PT-BR corretamente no assistente do Windows.
5. **`Arduino_GFX` fixado em `@ 1.5.5`:** Versões `1.6.x` quebram o build no core `espressif32 @ 7.0.1` do PlatformIO. Não atualizar sem migrar a plataforma inteira.
6. **Ordem de inicialização AXS15231B:** O barramento QSPI (`gfx->begin()`) **deve** inicializar antes do I²C do touch (`Wire.begin(4, 8)`).
7. **Símbolo `°` e fontes LVGL 8.3:** As fontes `lv_font_montserrat_*` não contêm `°`; temperaturas usam `inter_bold_16` (`"\xC2\xB0" "C"`). O relógio do descanso de tela usa `LV_FONT_MONTSERRAT_48`.
8. **Teclado virtual LVGL (`screen_wifi_manager`):** Deve usar `lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0)` com tamanho `480×240` (evita recorte de teclas fora da tela).
9. **Anti-Standby centralizado:** Todos os gatilhos de descanso de tela (`pc_idle`, queda de WebSocket e timer de standby) passam por `ui_enter_screensaver()` em `firmware/src/ui.cpp`, que bloqueia a transição quando a tela ativa for `UI_SCREEN_HARDWARE`, `UI_SCREEN_GAMES` ou `UI_SCREEN_GAME_HUD` (ou enquanto o modal Wi-Fi estiver aberto).
10. **Credenciais sanitizadas:** `firmware/include/secrets.h` está no `.gitignore`; nunca commitar SSIDs/senhas.
11. **Telemetria sem Admin, CS2 GSI e Hot-Plug WASAPI:** `nvml.dll` + `pdh.dll` via `ctypes` (sem subprocessos), `WiFi.setSleep(false)` + `TCP_NODELAY` + `Expect: 100-continue`/`chunked` na porta `3000`, e todas as chamadas COM/WASAPI mantidas na mesma thread do loop `asyncio`.

---

## 4. Comandos Rápidos de Build e Operação

```powershell
# Compilar e gravar firmware no ESP32-S3 (COM8)
& "C:\Users\mathe\.platformio\penv\Scripts\pio.exe" run -d firmware --target upload

# Gerar o instalador oficial único (dist\MiniDeckSetup.exe)
.\build_installer.bat

# Iniciar ou encerrar a bridge silenciosamente (desenvolvimento local)
wscript.exe backend\iniciar_silencioso.vbs
wscript.exe backend\parar_bridge.vbs

# Testar CS2 GSI localmente (porta 3000)
Invoke-RestMethod -Uri "http://127.0.0.1:3000" -Method Post -ContentType "application/json" -Body '{"round":{"bomb":"planted"},"bomb":{"state":"planted"}}'
```

---

## 5. Próximos Passos Sugeridos (Roadmap v2.4+)

1. **Publicar GitHub Release `v2.3` com binário anexado:** anexar `dist/MiniDeckSetup.exe` e `backend/gamestate_integration_minideck.cfg` à Release `v2.3` no GitHub para download direto em 1 clique.
2. **Ícone dedicado (`minideck.ico`):** criar ícone próprio para o instalador `MiniDeckSetup.exe`, atalhos do Menu Iniciar/Área de Trabalho e desinstalador.
3. **Atualização OTA via Wi-Fi (ESP32):** permitir atualizar o firmware sem cabo USB pela rede local (`ArduinoOTA` / HTTP upload).
4. **Brilho do Monitor Windows (DDC/CI) & Perfis por Jogo:** adicionar controle opcional de brilho do monitor principal do PC e perfis automáticos de áudio/atalhos por jogo ativo.
