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

rem VSROOT is resolved by vswhere: the installed major version drifts (VS2022 -> VS18 on
rem this host) while the old hardcoded path can linger as an empty directory and pass an
rem "exists" probe. Fall back to the historical path only when vswhere reports nothing.
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
)
if not defined VSROOT set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
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
rem Ninja ships with VS but vcvars64 does not put it on PATH. CMake only falls back to the
rem bundled copy for the VS generator; with Ninja the executable must be findable.
set "VSNINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if exist "%VSNINJA%\ninja.exe" set "PATH=%VSNINJA%;%PATH%"
set "INCLUDE=%VCTOOLS%\%VC_VER%\include;%SDKROOT%\Include\%SDK_VER%\ucrt;%SDKROOT%\Include\%SDK_VER%\um;%SDKROOT%\Include\%SDK_VER%\shared;%SDKROOT%\Include\%SDK_VER%\winrt;%SDKROOT%\Include\%SDK_VER%\cppwinrt;%INCLUDE%"
set "LIB=%VCTOOLS%\%VC_VER%\lib\x64;%SDKROOT%\Lib\%SDK_VER%\ucrt\x64;%SDKROOT%\Lib\%SDK_VER%\um\x64;%LIB%"

cd /d "%~dp0.."
%*
exit /b %ERRORLEVEL%
