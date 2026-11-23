; =============================================================================
; Mini Deck Bridge v2.3 - Script do Instalador Inno Setup 6
; Gera o instalador unico em dist\MiniDeckSetup.exe com runtime Python portatil
; oficialmente assinado pela Python Software Foundation (imune ao Smart App Control).
; =============================================================================

#define MyAppName "Mini Deck Bridge"
#define MyAppVersion "2.3"
#define MyAppPublisher "Mini Deck IoT"
#define MyAppURL "https://github.com/MatheusCostaVaz/mini-deck"
#define MyStartScript "iniciar_silencioso.vbs"
#define MyStopScript "parar_bridge.vbs"

[Setup]
AppId={{B4E7A92D-3F5C-4D8E-9A11-7C6F2E8D4B23}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={localappdata}\MiniDeck
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=MiniDeckSetup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\python\pythonw.exe
CloseApplications=no

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "Iniciar automaticamente com o Windows (recomendado)"; GroupDescription: "Inicialização automática:"
Name: "desktopicon"; Description: "Criar atalhos na Área de Trabalho (Iniciar, Parar e Configurar)"; GroupDescription: "Atalhos adicionais:"; Flags: unchecked

[Files]
; Scripts principais da bridge
Source: "wifi_bridge.py"; DestDir: "{app}"; Flags: ignoreversion
Source: "audio_controller.py"; DestDir: "{app}"; Flags: ignoreversion
Source: "media_controller.py"; DestDir: "{app}"; Flags: ignoreversion
Source: "gamestate_integration_minideck.cfg"; DestDir: "{app}"; Flags: ignoreversion
; Preserva o config.json personalizado do usuario em reinstalacoes/atualizacoes
Source: "config.json"; DestDir: "{app}"; Flags: onlyifdoesntexist
; Executores silenciosos (sem janela de console)
Source: "iniciar_silencioso.vbs"; DestDir: "{app}"; Flags: ignoreversion
Source: "parar_bridge.vbs"; DestDir: "{app}"; Flags: ignoreversion
; Runtime Python portatil (assinado) + dependencias pre-empacotadas
Source: "runtime_portable\*"; DestDir: "{app}\python"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; Autostart silencioso na inicializacao do usuario atual
Name: "{userstartup}\MiniDeckBridge"; Filename: "{app}\{#MyStartScript}"; WorkingDir: "{app}"; IconFilename: "{app}\python\pythonw.exe"; Comment: "Mini Deck IoT Bridge (Execução Silenciosa)"; Tasks: autostart

; Atalhos no Menu Iniciar
Name: "{autoprograms}\{#MyAppName}\Iniciar {#MyAppName}"; Filename: "{app}\{#MyStartScript}"; WorkingDir: "{app}"; IconFilename: "{app}\python\pythonw.exe"; Comment: "Inicia o Mini Deck Bridge em segundo plano"
Name: "{autoprograms}\{#MyAppName}\Parar {#MyAppName}"; Filename: "{app}\{#MyStopScript}"; WorkingDir: "{app}"; IconFilename: "{sys}\shell32.dll"; IconIndex: 131; Comment: "Encerra o Mini Deck Bridge em execução"
Name: "{autoprograms}\{#MyAppName}\Editar Jogos e Atalhos (config.json)"; Filename: "notepad.exe"; Parameters: """{app}\config.json"""; WorkingDir: "{app}"; Comment: "Abre o arquivo de configuração dinâmica de jogos e macros"
Name: "{autoprograms}\{#MyAppName}\Abrir Pasta de Instalação"; Filename: "{app}"; Comment: "Abre a pasta de instalação e logs do Mini Deck"
Name: "{autoprograms}\{#MyAppName}\Desinstalar {#MyAppName}"; Filename: "{uninstallexe}"

; Atalhos opcionais na Area de Trabalho
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyStartScript}"; WorkingDir: "{app}"; IconFilename: "{app}\python\pythonw.exe"; Comment: "Iniciar Mini Deck Bridge silenciosamente"; Tasks: desktopicon
Name: "{autodesktop}\Parar {#MyAppName}"; Filename: "{app}\{#MyStopScript}"; WorkingDir: "{app}"; IconFilename: "{sys}\shell32.dll"; IconIndex: 131; Comment: "Parar Mini Deck Bridge"; Tasks: desktopicon
Name: "{autodesktop}\Configurar Mini Deck (config.json)"; Filename: "notepad.exe"; Parameters: """{app}\config.json"""; WorkingDir: "{app}"; Comment: "Editar jogos e atalhos do Mini Deck"; Tasks: desktopicon

[Run]
; Opcao marcada por padrao na tela final ("Concluir")
Filename: "wscript.exe"; Parameters: """{app}\{#MyStartScript}"""; WorkingDir: "{app}"; Description: "Iniciar o Mini Deck Bridge agora"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "wscript.exe"; Parameters: """{app}\{#MyStopScript}"""; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "StopMiniDeckBridge"

[UninstallDelete]
Type: files; Name: "{app}\wifi_bridge.log*"
Type: filesandordirs; Name: "{app}\__pycache__"
Type: files; Name: "{userstartup}\MiniDeckBridge.lnk"

[Code]
procedure StopRunningBridge;
var
  ResultCode: Integer;
  StopScriptPath: String;
begin
  try
    ExtractTemporaryFile('parar_bridge.vbs');
    StopScriptPath := ExpandConstant('{tmp}\parar_bridge.vbs');
    if FileExists(StopScriptPath) then
    begin
      Exec(ExpandConstant('{sys}\wscript.exe'), '"' + StopScriptPath + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      Sleep(500);
    end;
  except
    // Prossegue mesmo se nao houver instancia em execucao
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopRunningBridge;
  Result := '';
end;
