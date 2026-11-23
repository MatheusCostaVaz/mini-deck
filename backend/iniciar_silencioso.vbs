Set WshShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
strDir = fso.GetParentFolderName(WScript.ScriptFullName)

' Evita instancias duplicadas do wifi_bridge.py em segundo plano
On Error Resume Next
Set objWMIService = GetObject("winmgmts:\\.\root\cimv2")
If Err.Number = 0 Then
    Set colProcesses = objWMIService.ExecQuery("Select ProcessId from Win32_Process Where (Name = 'pythonw.exe' Or Name = 'python.exe') And CommandLine Like '%wifi_bridge.py%'")
    If colProcesses.Count > 0 Then
        WScript.Quit 0
    End If
End If
On Error GoTo 0

strPortablePy = strDir & "\python\pythonw.exe"
strVenvPy = strDir & "\.venv\Scripts\pythonw.exe"

If fso.FileExists(strPortablePy) Then
    strPy = strPortablePy
Else
    strPy = strVenvPy
End If

If Not fso.FileExists(strPy) Then
    WScript.Quit 1
End If

strBridge = strDir & "\wifi_bridge.py"
WshShell.CurrentDirectory = strDir
WshShell.Run """" & strPy & """ """ & strBridge & """", 0, False
