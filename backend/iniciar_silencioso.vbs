Set WshShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
strDir = fso.GetParentFolderName(WScript.ScriptFullName)
strVenvPy = strDir & "\.venv\Scripts\pythonw.exe"
strBridge = strDir & "\wifi_bridge.py"
WshShell.CurrentDirectory = strDir
WshShell.Run """" & strVenvPy & """ """ & strBridge & """", 0, False
