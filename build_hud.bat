@echo off
cd /d "%~dp0"
setlocal

set VC_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133
set SDK_DIR=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0
set SDK_LIB_DIR=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0
set PATH=%VC_DIR%\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64;%PATH%
set INCLUDE=%VC_DIR%\include;%SDK_DIR%\um;%SDK_DIR%\shared;%SDK_DIR%\ucrt
set LIB=%VC_DIR%\lib\onecore\x64;%SDK_LIB_DIR%\ucrt\x64;%SDK_LIB_DIR%\um\x64;%VC_DIR%\lib\x64

set OPUS_DIR=src\opus
set CFLAGS=/nologo /O1 /MT /W3 /utf-8 /DWIN32_LEAN_AND_MEAN /DCOBJMACROS /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /D_WIN32_WINNT=0x0600 /I. /I%OPUS_DIR%\include\ /I%OPUS_DIR%
set LIBS=opus.lib user32.lib gdi32.lib ole32.lib shell32.lib ws2_32.lib winmm.lib uuid.lib

echo === Qmini HUD Edition Build ===
del *.obj 2>nul

:: Swap panel.h with panel_hud.h
ren src\panel.h panel_orig.h
ren src\panel_hud.h panel.h

cl.exe %CFLAGS% /c src\main.c src\audio_capture.c src\audio_playback.c src\codec.c src\jitter_buffer.c src\network.c src\signaling.c src\hotkey.c src\config.c src\dialog.c src\notify.c src\crypto.c src\tiny_aes.c src\aec.c src\agc.c src\ns.c src\congestion.c src\sfu_client.c src\logger.c src\panel_hud.c
set R=%ERRORLEVEL%

:: Restore headers
ren src\panel.h panel_hud.h
ren src\panel_orig.h panel.h

if %R% neq 0 (echo COMPILE FAIL & exit /b 1)

:: panel_hud.obj provides the panel_* symbols for main.obj
rc /nologo /fo dialog.res src\dialog.rc
link /nologo /out:qmini_hud.exe /subsystem:windows *.obj dialog.res %LIBS%
if %ERRORLEVEL% neq 0 (echo LINK FAIL & exit /b 1)

echo === HUD build complete: qmini_hud.exe ===
dir qmini_hud.exe
del *.obj dialog.res 2>nul
