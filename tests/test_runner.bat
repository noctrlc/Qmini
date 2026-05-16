@echo off
cd /d "%~dp0\.."
setlocal enabledelayedexpansion

set VC_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133
set SDK_DIR=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0
set SDK_LIB_DIR=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0
set PATH=%VC_DIR%\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64;%PATH%
set INCLUDE=%VC_DIR%\include;%SDK_DIR%\um;%SDK_DIR%\shared;%SDK_DIR%\ucrt
set LIB=%VC_DIR%\lib\onecore\x64;%SDK_LIB_DIR%\ucrt\x64;%SDK_LIB_DIR%\um\x64;%VC_DIR%\lib\x64

set CFLAGS=/nologo /O1 /MT /W3 /utf-8 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /I. /Isrc\
set LFLAGS=/link /SUBSYSTEM:CONSOLE
set FAILED=0

echo ========================================
echo   QminiDoctor Test Runner
echo ========================================
echo.

REM --- test_ringbuf (header-only) ---
echo --- test_ringbuf ---
cl.exe %CFLAGS% /Fe:tests\test_ringbuf.exe tests\test_ringbuf.c %LFLAGS% >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   COMPILE FAILED
    set /a FAILED+=1
) else (
    tests\test_ringbuf.exe
    if !ERRORLEVEL! neq 0 set /a FAILED+=1
    del tests\test_ringbuf.exe tests\test_ringbuf.obj 2>nul
)
echo.

REM --- test_jitter (needs jitter_buffer.c) ---
echo --- test_jitter ---
cl.exe %CFLAGS% /Fe:tests\test_jitter.exe tests\test_jitter.c src\jitter_buffer.c %LFLAGS% >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   COMPILE FAILED
    set /a FAILED+=1
) else (
    tests\test_jitter.exe
    if !ERRORLEVEL! neq 0 set /a FAILED+=1
    del tests\test_jitter.exe tests\test_jitter.obj src\jitter_buffer.obj 2>nul
)
echo.

REM --- test_congestion (needs congestion.c) ---
echo --- test_congestion ---
cl.exe %CFLAGS% /Fe:tests\test_congestion.exe tests\test_congestion.c src\congestion.c %LFLAGS% >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   COMPILE FAILED
    set /a FAILED+=1
) else (
    tests\test_congestion.exe
    if !ERRORLEVEL! neq 0 set /a FAILED+=1
    del tests\test_congestion.exe tests\test_congestion.obj src\congestion.obj 2>nul
)
echo.

REM --- test_aec (needs aec.c) ---
echo --- test_aec ---
cl.exe %CFLAGS% /Fe:tests\test_aec.exe tests\test_aec.c src\aec.c %LFLAGS% >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   COMPILE FAILED
    set /a FAILED+=1
) else (
    tests\test_aec.exe
    if !ERRORLEVEL! neq 0 set /a FAILED+=1
    del tests\test_aec.exe tests\test_aec.obj src\aec.obj 2>nul
)
echo.

REM --- test_crypto (needs crypto.c + tiny_aes.c + advapi32) ---
echo --- test_crypto ---
cl.exe %CFLAGS% /Fe:tests\test_crypto.exe tests\test_crypto.c src\crypto.c src\tiny_aes.c %LFLAGS% advapi32.lib >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   COMPILE FAILED
    set /a FAILED+=1
) else (
    tests\test_crypto.exe
    if !ERRORLEVEL! neq 0 set /a FAILED+=1
    del tests\test_crypto.exe tests\test_crypto.obj src\crypto.obj src\tiny_aes.obj 2>nul
)
echo.

echo ========================================
if !FAILED!==0 (
    echo   ALL TESTS PASSED
) else (
    echo   !FAILED! test(s) FAILED
)
echo ========================================
exit /b !FAILED!
