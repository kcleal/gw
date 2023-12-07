''' Download gw on msys2 '''
set msysShell = CreateObject("WScript.Shell")
msysShell.run "C:\msys64\ucrt64.exe pacman -Sy --noconfirm mingw-w64-ucrt-x86_64-gw", 0, True

''' Add gw to PATH so can be used in CMD and PS '''
Function AddPath(ByVal PathToAdd)
  ' Adds a new path to the user path
  ' unless the path is already present
  ' in either user or system path.

  Dim oWSH, uENV, pENV, UserPath, PathElement, PathExists, OldPath

  Set oWSH = CreateObject("Wscript.Shell")
  Set uENV = oWSH.Environment("USER")
  Set pENV = oWSH.Environment("PROCESS")
  UserPath = uENV("path")


  ' Check if path already exists:
  OldPath = Split(pENV("path"), ";", -1, vbTextCompare)
  PathExists = False
  For Each PathElement In OldPath
    If StrComp(PathElement, PathToAdd, vbTextCompare) = 0 Then
      PathExists = True
      AddPath = False
      Exit For
    End If
  Next

  ' Only do if path not already exists:
  If Not PathExists Then
    ' Strip off trailing semicolons if present:
    Do While Right(UserPath, 1) = ";"
      UserPath = Left(UserPath, Len(UserPath) - 1)
    Loop

    ' Add new path to current path:
    If UserPath = "" Then
      UserPath = PathToAdd
    Else
      UserPath = UserPath & ";" & PathToAdd
    End If

    ' Set the new path into environment:
    uENV("path") = UserPath
    AddPath = True
  End If
  
  ' Destroy the objects:
  Set uENV = Nothing
  Set pENV = Nothing
  Set oWSH = Nothing
End Function

AddPath("C:\msys64\ucrt64\bin")

''' Write .bat launch script '''
Set fso=createobject("Scripting.FileSystemObject")
Set qfile=fso.OpenTextFile("C:\msys64\home\launch_gw.bat",2,True)
qfile.Write  "C:\msys64\ucrt64.exe gw"
qfile.Close
Set qfile=nothing
Set fso=nothing

''' Create shortcut to desktop '''
Const strProgramTitle = "gw"
Const strProgram = "C:\msys64\home\launch_gw.bat"
Const strWorkDir = "C:\msys64\home\"
Dim objShortcut, objShell
Set objShell = WScript.CreateObject ("Wscript.Shell")
strLPath = objShell.SpecialFolders ("Desktop")
strAPath = objShell.SpecialFolders ("AppData")
WinDir = objShell.ExpandEnvironmentStrings("%WinDir%") ' tmp
Set objShortcut = objShell.CreateShortcut (strLPath & "\" & strProgramTitle & ".lnk")
objShortcut.TargetPath = strProgram
objShortcut.WorkingDirectory = strWorkDir
objShortcut.Description = strProgramTitle
' change icon
' objShortcut.IconLocation = Windir & "\System32\moricons.dll,6"
' objShortcut.Save

''' Downlaod icon '''
Sub HTTPDownload( myURL, myPath )
    Dim i, objFile, objFSO, objHTTP, strFile, strMsg
    Const ForReading = 1, ForWriting = 2, ForAppending = 8
    Set objFSO = CreateObject( "Scripting.FileSystemObject" )
    If objFSO.FolderExists( myPath ) Then
        strFile = objFSO.BuildPath( myPath, Mid( myURL, InStrRev( myURL, "/" ) + 1 ) )
    ElseIf objFSO.FolderExists( Left( myPath, InStrRev( myPath, "\" ) - 1 ) ) Then
        strFile = myPath
    Else
        WScript.Echo "ERROR: Target folder not found."
        Exit Sub
    End If
    Set objFile = objFSO.OpenTextFile( strFile, ForWriting, True )
    Set objHTTP = CreateObject( "WinHttp.WinHttpRequest.5.1" )
    objHTTP.Open "GET", myURL, False
    objHTTP.Send
    For i = 1 To LenB( objHTTP.ResponseBody )
        objFile.Write Chr( AscB( MidB( objHTTP.ResponseBody, i, 1 ) ) )
    Next
    objFile.Close( )
End Sub

''' (strAPath & "\" & strProgramTitle & "\gw_icon.ico") '''
HTTPDownload "https://raw.githubusercontent.com/kcleal/gw/master/include/gw_icon.ico", (strAPath & "\gw_icon.ico")

' change icon
objShortcut.IconLocation = (strAPath & "\gw_icon.ico")
objShortcut.Save

''' Give admin priv - required for adding to applications folder '''
If Not WScript.Arguments.Named.Exists("elevate") Then
  CreateObject("Shell.Application").ShellExecute WScript.FullName _
    , """" & WScript.ScriptFullName & """ /elevate", "", "runas", 1
  WScript.Quit
End If

''' Add to applications folder '''
Set objShortcutP = objShell.CreateShortcut ("C:\ProgramData\Microsoft\Windows\Start Menu\Programs\gw.lnk")
objShortcutP.TargetPath = strProgram
objShortcutP.WorkingDirectory = strWorkDir
objShortcutP.Description = strProgramTitle
objShortcutP.IconLocation = (strAPath & "\gw_icon.ico")
objShortcutP.Save

WScript.Quit
