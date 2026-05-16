@echo off
cd /d "%~dp0"
setlocal

set VC_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133
set SDK_DIR=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0
set SDK_LIB_DIR=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0
set PATH=%VC_DIR%\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64;%PATH%
set INCLUDE=%VC_DIR%\include;%SDK_DIR%\um;%SDK_DIR%\shared;%SDK_DIR%\ucrt
set LIB=%VC_DIR%\lib\onecore\x64;%SDK_LIB_DIR%\ucrt\x64;%SDK_LIB_DIR%\um\x64;%VC_DIR%\lib\x64

set WINVER=/D_WIN32_WINNT=0x0600

set OPUS_DIR=src\opus

if not exist %OPUS_DIR%\src\opus.c (
    echo == Downloading libopus...
    if not exist opus.tar.gz (
        powershell -Command "[Net.ServicePointManager]::SecurityProtocol = 'tls12'; Invoke-WebRequest -Uri 'https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz' -OutFile 'opus.tar.gz'"
    )
    if exist opus.tar.gz (
        echo == Extracting opus...
        tar -xzf opus.tar.gz -C src\
        if exist src\opus-1.5.2 move src\opus-1.5.2 %OPUS_DIR%
        if exist src\opus-1.4 move src\opus-1.4 %OPUS_DIR%
    ) else (
        echo Failed to download opus. Place opus source in %OPUS_DIR% manually.
        exit /b 1
    )
)
if not exist %OPUS_DIR%\config.h (
    >%OPUS_DIR%\config.h echo #define PACKAGE_VERSION "1.5.2"
    >>%OPUS_DIR%\config.h echo #define OPUS_BUILD
    >>%OPUS_DIR%\config.h echo #define HAVE_LRINTF 1
    >>%OPUS_DIR%\config.h echo #define HAVE_LRINT 1
)

echo == Compiling opus static library...
set OPUS_CFLAGS=/nologo /O1 /MT /W3 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /DHAVE_CONFIG_H /DUSE_ALLOCA /DFLOATING_POINT /I%OPUS_DIR%\ /I%OPUS_DIR%\include\ /I%OPUS_DIR%\celt\ /I%OPUS_DIR%\silk\ /I%OPUS_DIR%\silk\float\

del *.obj 2>nul

if exist opus_files.txt del opus_files.txt
dir /b /s %OPUS_DIR%\src\*.c | findstr /V demo | findstr /V compare | findstr /V projection | findstr /V mapping > opus_files.txt
dir /b /s %OPUS_DIR%\celt\*.c | findstr /V demo | findstr /V arm | findstr /V tests | findstr /V opus_custom >> opus_files.txt
dir /b /s %OPUS_DIR%\silk\*.c | findstr /V demo | findstr /V x86 | findstr /V arm | findstr /V neon | findstr /V fixed | findstr /V tests >> opus_files.txt
dir /b /s %OPUS_DIR%\silk\float\*.c | findstr /V arm | findstr /V tests | findstr /V avx >> opus_files.txt

cl.exe %OPUS_CFLAGS% /c @opus_files.txt
if %ERRORLEVEL% neq 0 (
    echo Opus compilation failed
    exit /b 1
)

echo Linking opus.lib...
lib /nologo /out:opus.lib *.obj
if %ERRORLEVEL% neq 0 (
    echo Opus library creation failed
    exit /b 1
)

echo == Compiling Qmini...
echo Compiling resources...
rc /nologo /fo dialog.res src\dialog.rc
if %ERRORLEVEL% neq 0 (
    echo Resource compilation failed
    exit /b 1
)

set CFLAGS=/nologo /O1 /MT /W3 /utf-8 /DWIN32_LEAN_AND_MEAN /DCOBJMACROS /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /D_WIN32_WINNT=0x0600 /I. /I%OPUS_DIR%\include\ /I%OPUS_DIR%\ /I"%SDK_DIR%\um" /I"%SDK_DIR%\shared"
set LIBS=opus.lib user32.lib gdi32.lib ole32.lib shell32.lib ws2_32.lib winmm.lib uuid.lib

cl.exe %CFLAGS% /c src\main.c src\audio_capture.c src\audio_playback.c src\codec.c src\jitter_buffer.c src\network.c src\signaling.c src\panel.c src\hotkey.c src\config.c src\dialog.c src\notify.c src\crypto.c src\tiny_aes.c src\aec.c src\agc.c src\ns.c src\congestion.c src\sfu_client.c src\logger.c
if %ERRORLEVEL% neq 0 (
    echo Qmini compilation failed
    exit /b 1
)

echo Linking qmini.exe...
link /nologo /out:qmini.exe /subsystem:windows *.obj dialog.res %LIBS%
if %ERRORLEVEL% neq 0 (
    echo Linking failed
    exit /b 1
)

echo == Build complete: qmini.exe
dir qmini.exe

echo == Cleaning up intermediate files...
if exist opus_files.txt del opus_files.txt
del *.obj 2>nul
