' ==============================================================================
' 1. Elevate to Administrator Immediately
' ==============================================================================
If Not WScript.Arguments.Named.Exists("elevate") Then
  CreateObject("Shell.Application").ShellExecute WScript.FullName _
    , """" & WScript.ScriptFullName & """ /elevate", "", "runas", 1
  WScript.Quit
End If

Dim objShell, fso, strAPath, strLPath
Set objShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
strLPath = objShell.SpecialFolders("Desktop")
strAPath = objShell.SpecialFolders("AppData")

' ==============================================================================
' 2. Download and Install 'gw' via MSYS2 UCRT64
' ==============================================================================
' We set MSYSTEM=UCRT64 and call bash directly to safely execute the pacman command.
Dim msysCommand
msysCommand = "cmd.exe /c set MSYSTEM=UCRT64 && C:\msys64\usr\bin\bash.exe --login -c ""pacman -Sy --noconfirm mingw-w64-ucrt-x86_64-gw"""
objShell.Run msysCommand, 0, True

' ==============================================================================
' 3. Add UCRT64 Binary Directory to PATH
' ==============================================================================
Function AddPath(ByVal PathToAdd)
  Dim uENV, pENV, UserPath, PathElement, PathExists, OldPath
  Set uENV = objShell.Environment("USER")
  Set pENV = objShell.Environment("PROCESS")
  UserPath = uENV("path")

  OldPath = Split(pENV("path"), ";", -1, vbTextCompare)
  PathExists = False
  For Each PathElement In OldPath
    If StrComp(Trim(PathElement), Trim(PathToAdd), vbTextCompare) = 0 Then
      PathExists = True
      Exit For
    End If
  Next

  If Not PathExists Then
    Do While Right(UserPath, 1) = ";"
      UserPath = Left(UserPath, Len(UserPath) - 1)
    Loop
    If UserPath = "" Then
      UserPath = PathToAdd
    Else
      UserPath = UserPath & ";" & PathToAdd
    End If
    uENV("path") = UserPath
    AddPath = True
  Else
    AddPath = False
  End If
End Function

AddPath("C:\msys64\ucrt64\bin")

' ==============================================================================
' 4. Create Launch Script (Optimized for Windows Terminal)
' ==============================================================================
Dim launchBatPath, qfile
launchBatPath = "C:\msys64\home\launch_gw.bat"

Set qfile = fso.OpenTextFile(launchBatPath, 2, True)

qfile.WriteLine "@echo off"
qfile.WriteLine "set MSYSTEM=UCRT64"

' Check if Windows Terminal (wt.exe) exists in the system path
qfile.WriteLine "where wt.exe >nul 2>nul"
qfile.WriteLine "if %errorlevel% equ 0 ("
' If WT exists, launch bash inside it with a clean title and focus
qfile.WriteLine "    wt.exe --title ""gw"" C:\msys64\usr\bin\bash.exe --login -c ""gw"""
qfile.WriteLine ") else ("
' Fallback for older Windows 10 machines without Windows Terminal
qfile.WriteLine "    C:\msys64\usr\bin\bash.exe --login -c ""gw"""
qfile.WriteLine ")"

qfile.Close
Set qfile = Nothing

' ==============================================================================
' 5. Download Icon safely (Binary Stream Fix)
' ==============================================================================
Sub HTTPDownloadBinary(myURL, targetFile)
    Dim objHTTP, objStream
    Set objHTTP = CreateObject("WinHttp.WinHttpRequest.5.1")
    objHTTP.Open "GET", myURL, False
    objHTTP.Send

    If objHTTP.Status = 200 Then
        Set objStream = CreateObject("ADODB.Stream")
        objStream.Type = 1 ' adTypeBinary
        objStream.Open
        objStream.Write objHTTP.ResponseBody
        objStream.SaveToFile targetFile, 2 ' adSaveCreateOverWrite
        objStream.Close
        Set objStream = Nothing
    End If
    Set objHTTP = Nothing
End Sub

Dim iconPath
iconPath = strAPath & "\gw_icon.ico"
HTTPDownloadBinary "https://raw.githubusercontent.com/kcleal/gw/master/include/gw_icon.ico", iconPath

' ==============================================================================
' 6. Create Shortcuts (Desktop & Start Menu)
' ==============================================================================
Const strProgramTitle = "gw"
Const strWorkDir = "C:\msys64\home\"
Dim objShortcut

' Desktop Shortcut
Set objShortcut = objShell.CreateShortcut(strLPath & "\" & strProgramTitle & ".lnk")
objShortcut.TargetPath = launchBatPath
objShortcut.WorkingDirectory = strWorkDir
objShortcut.Description = strProgramTitle
objShortcut.IconLocation = iconPath
objShortcut.Save

' Start Menu Shortcut (All Users)
Dim startMenuPath
startMenuPath = "C:\ProgramData\Microsoft\Windows\Start Menu\Programs\gw.lnk"
Set objShortcut = objShell.CreateShortcut(startMenuPath)
objShortcut.TargetPath = launchBatPath
objShortcut.WorkingDirectory = strWorkDir
objShortcut.Description = strProgramTitle
objShortcut.IconLocation = iconPath
objShortcut.Save

WScript.Quit