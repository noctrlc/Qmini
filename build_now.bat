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

echo == Compiling Qmini (quick build)...
del *.obj 2>nul
del tests\*.obj 2>nul
del src\*.obj 2>nul
rc /nologo /fo dialog.res src\dialog.rc
if %ERRORLEVEL% neq 0 (echo RES FAIL & exit /b 1)

cl.exe %CFLAGS% /c src\main.c src\audio_capture.c src\audio_playback.c src\codec.c src\jitter_buffer.c src\network.c src\signaling.c src\hotkey.c src\config.c src\dialog.c src\notify.c src\crypto.c src\tiny_aes.c src\aec.c src\agc.c src\ns.c src\congestion.c src\sfu_client.c src\logger.c
cl.exe %CFLAGS% /TP /c src\panel.c
if %ERRORLEVEL% neq 0 (echo COMPILE FAIL & exit /b 1)

echo == Linking...
link /nologo /out:qmini.exe /subsystem:windows *.obj dialog.res %LIBS%
if %ERRORLEVEL% neq 0 (echo LINK FAIL & exit /b 1)

echo == SUCCESS
dir qmini.exe
del *.obj 2>nul
del dialog.res 2>nul
