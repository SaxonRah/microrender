@echo off
rem MicroRender run driver. Invoked through mr.bat; see there for usage.
rem
rem Replaces run_dosbox_mrender.bat, run_dosbox_dirty.bat, run_stress_dosbox.bat
rem and the four run_stress_<n>[_renderonly]_dosbox.bat variants, which differed
rem only in the arguments passed to the DOS binary.
setlocal EnableExtensions EnableDelayedExpansion
if "%MR_ROOT%"=="" set "MR_ROOT=%~dp0.."
cd /d "%MR_ROOT%"

if /i "%~1"=="run" shift /1
set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=dos"
shift /1

if /i "%WHAT%"=="dos"       goto r_dos
if /i "%WHAT%"=="dosraw"    goto r_dosraw
if /i "%WHAT%"=="stress"    goto r_stress
if /i "%WHAT%"=="stressraw" goto r_stressraw
if /i "%WHAT%"=="tests"     goto r_tests
if /i "%WHAT%"=="bench"     goto r_bench
if /i "%WHAT%"=="raylib"    goto r_raylib
echo ERROR: unknown run target "%WHAT%". Try: dos, dosraw, stress, stressraw, raylib, tests, bench.
exit /b 1

rem ---------------------------------------------------------------------------
:r_tests
set "PRESET=debug"
if /i "%~1"=="index8" set "PRESET=index8"
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
pushd "%MR_ROOT%\tests" >nul
if errorlevel 1 exit /b 1
ctest --preset "%PRESET%"
set "TEST_RC=!ERRORLEVEL!"
popd >nul
exit /b !TEST_RC!

:r_bench
set "FRAMES=%~1"
if "%FRAMES%"=="" set "FRAMES=200"
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
pushd "%MR_ROOT%\tests" >nul
if errorlevel 1 exit /b 1
cmake --preset bench
if errorlevel 1 (
    set "BENCH_RC=!ERRORLEVEL!"
    popd >nul
    exit /b !BENCH_RC!
)
cmake --build --preset bench --parallel
if errorlevel 1 (
    set "BENCH_RC=!ERRORLEVEL!"
    popd >nul
    exit /b !BENCH_RC!
)
popd >nul
set "BENCH_EXE=%MR_ROOT%\build\bench\mr_test_bench.exe"
if not exist "!BENCH_EXE!" set "BENCH_EXE=%MR_ROOT%\build\bench\Release\mr_test_bench.exe"
if not exist "!BENCH_EXE!" (
    echo ERROR: benchmark executable was not produced.
    exit /b 1
)
"!BENCH_EXE!" %FRAMES%
exit /b %ERRORLEVEL%

rem ---------------------------------------------------------------------------
:r_raylib
set "MR_FORWARD_ARGS="
:r_raylib_args
if "%~1"=="" goto r_raylib_launch
set "MR_FORWARD_ARGS=!MR_FORWARD_ARGS! "%~1""
shift /1
goto r_raylib_args
:r_raylib_launch
set "RAYEXE=%MR_ROOT%\build\raylib\microrender_raylib.exe"
if not exist "%RAYEXE%" set "RAYEXE=%MR_ROOT%\build\raylib\Release\microrender_raylib.exe"
if not exist "%RAYEXE%" (
    echo ERROR: Raylib frontend not built.
    echo Build it first: mr build raylib
    exit /b 1
)
"%RAYEXE%" !MR_FORWARD_ARGS!
exit /b %ERRORLEVEL%

rem ---------------------------------------------------------------------------
:r_dos
set "EXE=mrender.exe"
goto collect_dos_args

:r_dosraw
set "EXE=mraw.exe"
goto collect_dos_args

:collect_dos_args
set "DOSARGS="
:collect_dos_args_loop
if "%~1"=="" goto launch
rem %~1 already strips surrounding quotes; re-adding them puts literal quote
rem characters into the DOS command tail, where they end up inside argv values.
rem A path like "dos.shot" is then not a valid DOS filename and fopen fails.
rem DOS arguments do not contain spaces, so unquoted is both safe and correct.
set "DOSARGS=!DOSARGS! %~1"
shift /1
goto collect_dos_args_loop

:r_stress
set "EXE=mstress.exe"
goto collect_stress_args

:r_stressraw
set "EXE=msraw.exe"

:collect_stress_args
set "SPRITES=%~1"
if "%SPRITES%"=="" set "SPRITES=512"
set "FRAMES=%~2"
if "%FRAMES%"=="" set "FRAMES=2100"
shift /1
shift /1
set "MR_FORWARD_ARGS="
:collect_stress_args_loop
if "%~1"=="" goto collect_stress_args_done
set "MR_FORWARD_ARGS=!MR_FORWARD_ARGS! "%~1""
shift /1
goto collect_stress_args_loop
:collect_stress_args_done
set "DOSARGS=/sprites %SPRITES% /frames %FRAMES% /novsync !MR_FORWARD_ARGS!"

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

rem machine=vgaonly keeps the 320x240 unchained Mode X path on real VGA
rem emulation instead of routing it through an SVGA compatibility layer.
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
>>"%CONF%" echo echo MicroRender: %EXE% !DOSARGS!  [cycles=%CYCLES%]
>>"%CONF%" echo %EXE% !DOSARGS!
>>"%CONF%" echo echo.
rem Capture scripts need this unattended. MR_DOSBOX_NOPAUSE=1 drops the
rem keypress so DOSBox closes on its own once the program returns.
if not "%MR_DOSBOX_NOPAUSE%"=="1" (
    >>"%CONF%" echo echo Finished. Press any key to close DOSBox.
    >>"%CONF%" echo pause
)
>>"%CONF%" echo exit

echo Launching %EXE% !DOSARGS! (cycles=%CYCLES%)
rem DOSBox is a GUI-subsystem executable, so cmd does not wait for it: calling
rem it directly returns immediately while DOSBox is still starting. That broke
rem two things at once. The config file below was deleted out from under DOSBox
rem before it had finished reading it, and capture scripts looked for output
rem files before the program had run at all.
rem
rem start /wait blocks until the process exits and still propagates its exit
rem code. The empty "" is the window title, which start requires when the first
rem quoted argument is the program path.
start /wait "" "%MR_DOSBOX%" -conf "%CONF%"
set "RC=%ERRORLEVEL%"
del "%CONF%" >nul 2>nul
exit /b %RC%
