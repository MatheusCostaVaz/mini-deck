' Encerra silenciosamente apenas os processos da bridge do Mini Deck (wifi_bridge.py)
On Error Resume Next
Set objWMIService = GetObject("winmgmts:\\.\root\cimv2")
If Err.Number = 0 Then
    Set colProcesses = objWMIService.ExecQuery("Select * from Win32_Process Where (Name = 'pythonw.exe' Or Name = 'python.exe') And CommandLine Like '%wifi_bridge.py%'")
    For Each objProcess in colProcesses
        objProcess.Terminate()
    Next
End If
