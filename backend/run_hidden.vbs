' Inicializador silencioso da Bridge Wi-Fi (WebSockets) do Mini Deck IoT (sem janela de console)
Option Explicit

Dim fso, shell, scriptDir, pythonwPath, bridgeScript, cmd

Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")

scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
pythonwPath = fso.BuildPath(scriptDir, ".venv\Scripts\pythonw.exe")
bridgeScript = fso.BuildPath(scriptDir, "wifi_bridge.py")

If Not fso.FileExists(pythonwPath) Then
    WScript.Quit 1
End If

If Not fso.FileExists(bridgeScript) Then
    WScript.Quit 2
End If

shell.CurrentDirectory = scriptDir
cmd = """" & pythonwPath & """ """ & bridgeScript & """"

' 0 = janela totalmente oculta, False = não aguarda encerramento
shell.Run cmd, 0, False
