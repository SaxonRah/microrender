@echo off
rem MicroRender run driver. Invoked through mr.bat; see there for usage.
rem
rem Replaces run_dosbox_mrender.bat, run_dosbox_dirty.bat, run_stress_dosbox.bat
rem and the four run_stress_<n>[_renderonly]_dosbox.bat variants, which differed
rem only in the arguments passed to the DOS binary.
setlocal EnableExtensions EnableDelayedExpansion
if "%MR_ROOT%"=="" set "MR_ROOT=%~dp0.."
cd /d "%MR_ROOT%"

set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=dos"

if /i "%WHAT%"=="dos"    goto r_dos
if /i "%WHAT%"=="stress" goto r_stress
if /i "%WHAT%"=="tests"  goto r_tests
if /i "%WHAT%"=="bench"  goto r_bench
echo ERROR: unknown run target "%WHAT%". Try: dos, stress, tests, bench.
exit /b 1

rem ---------------------------------------------------------------------------
:r_tests
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
set "PRESET=debug"
if /i "%~2"=="index8" set "PRESET=index8"
ctest --preset "%PRESET%"
exit /b %ERRORLEVEL%

:r_bench
set "FRAMES=%~2"
if "%FRAMES%"=="" set "FRAMES=200"
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
cmake --preset bench -S "%MR_ROOT%\tests"
if errorlevel 1 exit /b 1
cmake --build --preset bench
if errorlevel 1 exit /b 1
"%MR_ROOT%\build\bench\mr_test_bench.exe" %FRAMES%
exit /b %ERRORLEVEL%

rem ---------------------------------------------------------------------------
:r_dos
set "EXE=mrender.exe"
set "DOSARGS=%~2 %~3 %~4 %~5 %~6 %~7 %~8 %~9"
goto launch

:r_stress
set "EXE=mstress.exe"
set "SPRITES=%~2"
if "%SPRITES%"=="" set "SPRITES=512"
set "FRAMES=%~3"
if "%FRAMES%"=="" set "FRAMES=2100"
set "DOSARGS=/sprites %SPRITES% /frames %FRAMES% /novsync %~4 %~5 %~6 %~7 %~8 %~9"
goto launch

rem ---------------------------------------------------------------------------
:launch
call "%MR_ROOT%\scripts\mr_tools.bat" dosbox
if errorlevel 1 exit /b 1

set "DOSROOT=%MR_ROOT%\microrender_dos\dosroot"
set "DIST=%MR_ROOT%\microrender_dos\dist"

if not exist "%DOSROOT%\%EXE%" (
    if exist "%DIST%\%EXE%" (
        if not exist "%DOSROOT%" mkdir "%DOSROOT%"
        copy /Y "%DIST%\%EXE%" "%DOSROOT%\%EXE%" >nul
    )
)
if not exist "%DOSROOT%\%EXE%" (
    echo ERROR: %EXE% not found in dosroot\ or dist\.
    echo Build it first:  mr build dos
    exit /b 1
)

rem cycles=max measures the host CPU, not a period machine. Override with
rem MR_DOSBOX_CYCLES to get a reproducible, quotable number:
rem   fixed 3000 ~386DX/33   fixed 12000 ~486DX2/66   fixed 30000 ~Pentium 100
set "CYCLES=%MR_DOSBOX_CYCLES%"
if "%CYCLES%"=="" set "CYCLES=max"

set "CONF=%TEMP%\microrender_%RANDOM%.conf"

rem machine=vgaonly keeps mode 13h on the real VGA path rather than through
rem SVGA emulation. Both runners need it; only one of the old ones had it.
> "%CONF%" echo [dosbox]
>>"%CONF%" echo machine=vgaonly
>>"%CONF%" echo [sdl]
>>"%CONF%" echo autolock=false
>>"%CONF%" echo [cpu]
>>"%CONF%" echo core=dynamic
>>"%CONF%" echo cycles=%CYCLES%
>>"%CONF%" echo [render]
>>"%CONF%" echo frameskip=0
>>"%CONF%" echo aspect=false
>>"%CONF%" echo [autoexec]
>>"%CONF%" echo mount c "%DOSROOT%"
>>"%CONF%" echo c:
>>"%CONF%" echo echo MicroRender: %EXE% %DOSARGS%  [cycles=%CYCLES%]
>>"%CONF%" echo %EXE% %DOSARGS%
>>"%CONF%" echo echo.
>>"%CONF%" echo echo Finished. Press any key to close DOSBox.
>>"%CONF%" echo pause
>>"%CONF%" echo exit

echo Launching %EXE% %DOSARGS% (cycles=%CYCLES%)
rem Run in the foreground so the exit code propagates to capture scripts.
"%MR_DOSBOX%" -conf "%CONF%"
set "RC=%ERRORLEVEL%"
del "%CONF%" >nul 2>nul
exit /b %RC%
