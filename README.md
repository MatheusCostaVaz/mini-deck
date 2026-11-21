# Mini Deck IoT (v1.0)

Controlador físico de áudio **100% sem fio** para **Windows 11**, construído com um módulo **ESP32-S3 3.5" Touch (Guition JC3248W535C)**, interface gráfica **LVGL** em tema escuro e serviço em segundo plano em **Python** integrado à Windows Core Audio API (WASAPI).

---

## Funcionalidades da v1.0

- **Controle Master:** Slider vertical de Volume Master (0–100%) com porcentagem ao vivo e botão de Mute.
- **Mixer por Aplicativo:** Sliders e botões de mute dedicados com detecção inteligente de sessões ativas no Windows:
  - **Navegador:** Edge, Chrome, Firefox, Brave, Opera, Vivaldi
  - **Chat:** Discord, Teams, Zoom, Slack, Telegram
  - **Geral / Jogos:** Steam, Spotify ou qualquer jogo/app de áudio ativo no momento (título atualizado dinamicamente na tela)
- **Botão Dedicado de Microfone:** Alternância rápida do Mute do microfone padrão com destaque visual (**Verde** quando ativo, **Vermelho** quando mutado).
- **100% Sem Fio (Wi-Fi + WebSockets):**
  - Servidor WebSocket na porta `81` rodando no ESP32-S3.
  - **Auto-Discovery UDP** (porta `4210`): o PC descobre automaticamente o IP do Mini Deck na rede local sem configuração manual de IP.
  - Sincronização bidirecional instantânea (alterações feitas no Windows refletem na tela e vice-versa) com reconexão automática contínua.
- **Relógio Digital NTP e Status de Rede:** Exibição da hora local (`HH:MM`, GMT-3 Brasil), IP do ESP32 e status de conexão com o PC no cabeçalho.
- **Autoinicialização Silenciosa no Windows 11:** Execução em segundo plano sem janela de terminal via `run_hidden.vbs` (`pythonw.exe`) com atalho automático em `shell:startup`.

---

## Hardware Utilizado

| Componente | Especificação |
| :--- | :--- |
| **Placa / Módulo** | Guition **JC3248W535C** (`esp32-s3-devkitc-1`) |
| **MCU & Memória** | ESP32-S3-WROOM-1-N16R8 (16 MB QIO Flash + 8 MB OPI PSRAM) |
| **Display** | 3.5" IPS 320×480 (modo paisagem 480×320) — Driver **AXS15231B** via **QSPI** (`Arduino_GFX` + `Arduino_Canvas` em PSRAM) |
| **Touch** | Capacitivo integrado **AXS15231B** via **I²C** (`SDA=GPIO4`, `SCL=GPIO8`, endereço `0x3B`) |
| **Interface Gráfica** | **LVGL 8.3.11** (Dark Theme) |

---

## Estrutura do Projeto

```text
mini-deck/
├── backend/
│   ├── audio_controller.py   # Controle WASAPI/Core Audio (Master, apps, microfone)
│   ├── wifi_bridge.py        # Bridge principal sem fio (UDP Auto-Discovery + WebSockets)
│   ├── serial_bridge.py      # Bridge alternativa via cabo USB Serial (fallback)
│   ├── run_hidden.vbs        # Inicializador silencioso via pythonw.exe
│   ├── setup_startup.py      # Cria/remove atalho na Inicialização do Windows (shell:startup)
│   ├── test_audio.py         # Script de diagnóstico rápido de áudio
│   └── requirements.txt      # Dependências Python congeladas
├── firmware/
│   ├── platformio.ini        # Configuração PlatformIO (ESP32-S3, PSRAM, bibliotecas)
│   ├── include/
│   │   ├── lv_conf.h         # Configuração LVGL otimizada para ESP32-S3
│   │   └── pins_config.h     # Mapeamento de pinos QSPI, Backlight e Touch I²C
│   └── src/
│       ├── main.cpp          # Display, Touch, Wi-Fi, NTP, WebSocket Server e UDP Broadcast
│       ├── ui.h / ui.cpp     # Interface LVGL (4 colunas de mixer + dock do microfone)
├── .gitignore
└── README.md
```

---

## Guia Rápido de Restauração (PC Formatado / Nova Máquina)

Caso o Windows seja formatado ou você queira configurar o serviço em outro computador, siga estes 4 passos dentro do repositório clonado:

1. **Instale o Python 3.10+** (marque a opção *Add Python to PATH*).
2. **Crie o ambiente virtual na pasta `backend/`:**
   ```powershell
   cd backend
   python -m venv .venv
   ```
3. **Ative o ambiente virtual e instale as dependências:**
   ```powershell
   .\.venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   ```
4. **Registre a inicialização automática com o Windows:**
   ```powershell
   python setup_startup.py
   ```

Pronto! Para iniciar imediatamente sem precisar reiniciar o PC, execute `run_hidden.vbs` (ou `python wifi_bridge.py` para acompanhar os logs no terminal).

*(Para remover da inicialização automática no futuro, execute `python setup_startup.py --remove`.)*

---

## Compilação e Gravação do Firmware (Opcional)

O firmware já fica gravado na memória flash do ESP32-S3 e não precisa ser regravado ao formatar o PC. Caso deseje compilar ou atualizar o firmware com **PlatformIO**:

```powershell
pio run -d firmware -t upload
```

> Ajuste a porta `upload_port` em `firmware/platformio.ini` (padrão `COM8`) e as credenciais de Wi-Fi em `firmware/src/main.cpp` conforme sua rede local.
