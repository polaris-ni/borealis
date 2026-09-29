@echo off
rem Run the given command inside the VS developer environment.
rem Usage: tools\msvc_env.bat <command> [args...]
rem
rem Why not rely on vcvars64.bat alone: it locates the Windows SDK via reg.exe, and the
rem sandbox blacklists reg.exe. When blocked, INCLUDE/LIB lose the CRT and SDK paths and
rem cl.exe fails with: fatal error C1083: cannot open include file: "crtdbg.h".
rem So the newest toolset / SDK directories are probed here and appended explicitly;
rem vcvars still runs (identical result when it succeeds, covered by this when blocked).
rem
rem Comments stay ASCII on purpose: cmd parses .bat as the local code page, and non-ASCII
rem bytes inside comments can be mis-read as commands (GBK-mojibake "not a command").

setlocal enabledelayedexpansion

set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "VCTOOLS=%VSROOT%\VC\Tools\MSVC"
set "SDKROOT=C:\Program Files (x86)\Windows Kits\10"

rem Highest version first: dir /o-n sorts descending.
for /f "delims=" %%d in ('dir /b /ad /o-n "%VCTOOLS%" 2^>nul') do (
    set "VC_VER=%%d"
    goto vc_done
)
:vc_done
for /f "delims=" %%d in ('dir /b /ad /o-n "%SDKROOT%\Include" 2^>nul') do (
    set "SDK_VER=%%d"
    goto sdk_done
)
:sdk_done

if not defined VC_VER (
    echo [msvc_env] MSVC toolset not found under %VCTOOLS% 1>&2
    exit /b 1
)

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul

set "PATH=%VCTOOLS%\%VC_VER%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%VCTOOLS%\%VC_VER%\include;%SDKROOT%\Include\%SDK_VER%\ucrt;%SDKROOT%\Include\%SDK_VER%\um;%SDKROOT%\Include\%SDK_VER%\shared;%SDKROOT%\Include\%SDK_VER%\winrt;%SDKROOT%\Include\%SDK_VER%\cppwinrt;%INCLUDE%"
set "LIB=%VCTOOLS%\%VC_VER%\lib\x64;%SDKROOT%\Lib\%SDK_VER%\ucrt\x64;%SDKROOT%\Lib\%SDK_VER%\um\x64;%LIB%"

cd /d "%~dp0.."
%*
exit /b %ERRORLEVEL%
