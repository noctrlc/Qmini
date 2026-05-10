@echo off
setlocal
set VC_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133
set SDK_DIR=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0
set SDK_LIB_DIR=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0
set PATH=%VC_DIR%\bin\Hostx64\x64;%PATH%
set INCLUDE=%VC_DIR%\include;%SDK_DIR%\um;%SDK_DIR%\shared;%SDK_DIR%\ucrt
set LIB=%VC_DIR%\lib\onecore\x64;%SDK_LIB_DIR%\ucrt\x64;%SDK_LIB_DIR%\um\x64;%VC_DIR%\lib\x64
cd /d D:\Qmini\signaling_server
cl.exe /nologo /MT /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0600 signaling_server.c /Fe:signaling_server.exe /link ws2_32.lib
exit %ERRORLEVEL%
