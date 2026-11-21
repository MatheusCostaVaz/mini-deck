# 🎛️ Mini Deck IoT - Controle de Áudio sem Fio para Windows 11

Dispositivo físico autônomo com tela touch LCD de 3.5" (ESP32-S3), desenvolvido para gerenciamento granular de áudio no Windows 11 de forma 100% sem fio (via Wi-Fi/WebSockets) ou via cabo USB (Serial).

---

## ✨ Funcionalidades

- **Controle Master:** Slider dedicado para volume principal (0–100%) e botão de mute geral.
- **Controle por Aplicativo:** Sliders e mutes independentes para Navegador (Edge/Chrome), Discord e Jogos (Steam/apps ativos).
- **Mute de Microfone:** Botão touch dedicado com alternância de estado visual (**Vermelho** = Mutado / **Verde** = Aberto).
- **Comunicação 100% Sem Fio:**
  - Conexão de baixa latência via **WebSockets** (porta `81`).
  - **Auto-Discovery UDP (porta `4210`):** o executável acha o IP do Mini Deck na rede local automaticamente.
  - Sincronização bidirecional instantânea e reconexão automática contínua.
- **Relógio Digital (NTP):** Sincronizado automaticamente com o horário de Brasília (`GMT-3`).
- **Executável Windows Autônomo (`.exe`):** Funciona sem necessidade de ter Python ou bibliotecas instaladas na máquina.

---

## 🚀 Guia Rápido (Se formatar o PC ou usar em outro Computador)

1. Ligue o Mini Deck na tomada (ele se conecta automaticamente ao Wi-Fi).
2. Certifique-se de que o computador está na mesma rede Wi-Fi.
3. Execute o `MiniDeckBridge.exe` (disponível em `backend/dist/` ou na aba **Releases** do repositório).
4. O aplicativo localizará o Mini Deck em segundos e sincronizará todos os volumes na hora.

> **Para iniciar sempre com o Windows:**  
> Pressione `Win + R`, digite `shell:startup` e cole um atalho do `MiniDeckBridge.exe` nessa pasta (ou execute `python backend/setup_startup.py`).

---

## 🛠️ Tecnologias

- **Hardware:** Guition **JC3248W535C** (ESP32-S3-N16R8, 16 MB Flash, 8 MB OPI PSRAM, display 3.5" IPS 480×320 QSPI AXS15231B + touch capacitivo I²C).
- **Firmware:** ESP32-S3 (PlatformIO / Arduino), `Arduino_GFX`, `LVGL 8.3`, `ArduinoJson`, `WebSocketsServer`.
- **Backend:** Python 3.14 (`pycaw`, `websockets`, `comtypes`), empacotado em `.exe` de arquivo único com **PyInstaller**.

---

## 📦 Recompilando do Código-Fonte (Opcional)

### Backend Windows
```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python setup_startup.py
```

Para gerar novamente o executável único (`backend/dist/MiniDeckBridge.exe`):
```powershell
pip install pyinstaller
pyinstaller --onefile --noconsole --clean --name MiniDeckBridge --distpath backend\dist --workpath backend\build --specpath backend --paths backend --hidden-import=comtypes.stream --hidden-import=comtypes.client --hidden-import=pycaw --hidden-import=websockets.legacy --hidden-import=websockets.legacy.server backend\wifi_bridge.py
```

### Firmware ESP32-S3
```powershell
pio run -d firmware -t upload
```
