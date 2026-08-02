@echo off
rem ---------------------------------------------------------------------------
rem Build mr_asset.exe with Open Watcom as a Windows NT/Win32 console tool.
rem This is a HOST tool for Windows 10/11, not a DOS executable.
rem ---------------------------------------------------------------------------

setlocal EnableExtensions
cd /d "%~dp0"

call :check_watcom
if errorlevel 1 goto fail

if exist mr_asset.exe del /q mr_asset.exe
if exist mr_asset.err del /q mr_asset.err

echo Building tools\mr_asset.exe with Open Watcom...
echo WATCOM: %WATCOM%
echo Compiler/linker: %OW_WCL386%

rem IMPORTANT:
rem Watcom -d defines must be glued to the option:
rem correct:   -dsnprintf=_snprintf
rem incorrect: -d snprintf=_snprintf
"%OW_WCL386%" -q -bt=nt -ox -w4 -dsnprintf=_snprintf -fe=mr_asset.exe mr_asset.c > mr_asset.err 2>&1

if errorlevel 1 (
    type mr_asset.err
    echo ERROR: mr_asset build failed.
    goto fail
)

echo.
echo Build OK: tools\mr_asset.exe
goto done

:check_watcom
if "%WATCOM%"=="" set "WATCOM=C:\WATCOM"

if exist "%WATCOM%\BINNT\wcl386.exe" (
    set "OW_WCL386=%WATCOM%\BINNT\wcl386.exe"
) else if exist "%WATCOM%\BINW\wcl386.exe" (
    set "OW_WCL386=%WATCOM%\BINW\wcl386.exe"
) else (
    echo ERROR: wcl386.exe not found under "%WATCOM%".
    echo Expected "%WATCOM%\BINNT\wcl386.exe" or "%WATCOM%\BINW\wcl386.exe".
    echo Set WATCOM to your Open Watcom install path, for example:
    echo set "WATCOM=C:\WATCOM"
    exit /b 1
)

set "PATH=%WATCOM%\BINNT;%WATCOM%\BINW;%PATH%"
set "INCLUDE=%WATCOM%\H;%WATCOM%\H\NT;%INCLUDE%"
set "EDPATH=%WATCOM%\EDDAT"

exit /b 0

:fail
echo.
echo Build FAILED.
exit /b 1

:done
endlocal