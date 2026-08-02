@echo off
rem Open a local Open Watcom command shell rooted at this MicroRender project.
rem Uses C:\WATCOM by default, or honors an existing WATCOM variable.
rem IMPORTANT: On 64-bit Windows, BINNT must come before BINW so the NT-hosted
rem compiler tools are used. BINW may contain 16-bit host tools Windows x64
rem cannot execute.

if "%WATCOM%"=="" set "WATCOM=C:\WATCOM"

if not exist "%WATCOM%\BINNT" (
    echo ERROR: Open Watcom NT-host tools not found at "%WATCOM%\BINNT".
    echo Install Open Watcom / Open Watcom V2, or edit WATCOM in this script.
    pause
    exit /b 1
)

set "PATH=%WATCOM%\BINNT;%WATCOM%\BINW;%PATH%"
set "INCLUDE=%WATCOM%\H;%WATCOM%\H\NT;%INCLUDE%"
set "EDPATH=%WATCOM%\EDDAT"
set "WHTMLHELP=%WATCOM%\BINNT\HELP"
set "WIPFC=%WATCOM%\WIPFC"

cd /d "%~dp0"

echo Open Watcom Build Environment
echo Project: %CD%
echo.
echo Using NT-hosted tools first:
echo %WATCOM%\BINNT
echo.
echo Common commands:
echo   build_watcom.bat dos16
echo   mr build dos   ^(from the repository root^)
echo   mr run dos     ^(from the repository root^)
echo.
"%COMSPEC%" /k
