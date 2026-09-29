@echo off
rem 在 VS 开发者环境下执行随附命令：本仓预设为 MSVC + Ninja，而 cl.exe 不在默认 PATH，
rem 故配置与构建须经 vcvars 环境。用法：tools\msvc_env.bat <命令> [参数...]
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
%*
exit /b %ERRORLEVEL%
