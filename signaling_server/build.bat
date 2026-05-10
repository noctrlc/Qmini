@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cl /nologo /O1 /MT /W3 signaling_server.c /Fesignaling_server.exe /link ws2_32.lib
if %ERRORLEVEL% equ 0 (
    echo Server built: signaling_server.exe
) else (
    echo Build failed
)
