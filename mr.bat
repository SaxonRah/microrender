@echo off
rem ===========================================================================
rem  MicroRender - single entry point for every build and run task.
rem
rem  This replaces the 37 individual .bat files the project used to carry, most
rem  of which differed only in a couple of hardcoded arguments. Variants are now
rem  arguments, not separate files.
rem
rem    mr build assets           regenerate GAME.MRP and the embedded C assets
rem    mr build dos [mode=both]  16-bit DOS optimized + raw executables
rem    mr build pico [preset|all] [key=value ...]
rem                              RP2350 firmware; settings are command-line flags
rem    mr build raylib [key=value ...]
rem                              desktop 320x240 RGB565 frontend
rem    mr build pico <preset> vscode
rem                              same, but configured into microrender\build so
rem                              the VS Code Flash and Debug buttons use it
rem    mr build tests            host test binaries
rem    mr build all              everything the local toolchain supports
rem
rem    mr run dos [args]         optimized game demo in DOSBox
rem    mr run dosraw [args]      raw draw-then-present baseline
rem    mr run stress [n] [f]     optimized stress test in DOSBox
rem    mr run stressraw [n] [f]  raw stress baseline
rem    mr run raylib [args...]    desktop game/stress frontend
rem    mr test [index8]          host unit + fuzz suite under ASan/UBSan
rem    mr bench [frames]         host benchmark
rem
rem    mr clean                  remove all build output
rem    mr help                   this text
rem
rem  Environment overrides:
rem    WATCOM              Open Watcom install root
rem    DOSBOX_EXE          full path to dosbox-x.exe or dosbox.exe
rem    MR_DOSBOX_CYCLES    DOSBox cycles; default "max". Period-accurate values:
rem                          fixed 3000   ~386DX/33
rem                          fixed 12000  ~486DX2/66
rem                          fixed 30000  ~Pentium 100
rem                        Benchmark numbers taken at "max" measure the host
rem                        CPU, not a period machine. Pin this before quoting
rem                        a framerate anywhere.
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"

set "MR_ROOT=%CD%"

rem Sanity check: this must run from the repository root, not from an unpacked
rem patch bundle. The patch archive contains only changed files, so the asset
rem pipeline and the rest of the sources will be missing there.
if not exist "%MR_ROOT%\shared\tools\mr_pack.py" goto not_a_repo
if not exist "%MR_ROOT%\shared\src\gfx.c" goto not_a_repo

set "CMD=%~1"
if "%CMD%"=="" set "CMD=help"
shift /1

if /i "%CMD%"=="build" goto do_build
if /i "%CMD%"=="run"   goto do_run
if /i "%CMD%"=="test"  goto do_test
if /i "%CMD%"=="bench" goto do_bench
if /i "%CMD%"=="clean" goto do_clean
if /i "%CMD%"=="help"  goto do_help
if /i "%CMD%"=="-h"    goto do_help
if /i "%CMD%"=="--help" goto do_help

echo ERROR: unknown command "%CMD%".
echo.
goto do_help

:do_build
rem Forward the complete original argument list; the worker strips the leading
rem "build" token. This avoids the old nine-argument ceiling on key=value
rem configuration overrides.
call "%MR_ROOT%\scripts\mr_build.bat" %*
exit /b %ERRORLEVEL%

:do_run
call "%MR_ROOT%\scripts\mr_run.bat" %*
exit /b %ERRORLEVEL%

:do_test
call "%MR_ROOT%\scripts\mr_build.bat" tests %1
if errorlevel 1 exit /b 1
call "%MR_ROOT%\scripts\mr_run.bat" tests %1
exit /b %ERRORLEVEL%

:do_bench
call "%MR_ROOT%\scripts\mr_run.bat" bench %1
exit /b %ERRORLEVEL%

:do_clean
call "%MR_ROOT%\scripts\mr_clean.bat"
exit /b %ERRORLEVEL%

:not_a_repo
echo ERROR: this does not look like the MicroRender repository root.
echo.
echo   Looking in: %MR_ROOT%
echo   Missing:    shared\tools\mr_pack.py and/or shared\src\gfx.c
echo.
echo If you unpacked the review patch archive, that contains only the files
echo that changed. Copy its contents over your existing checkout and run
echo mr.bat from there:
echo.
echo   robocopy ^<patch-folder^> ^<repo-folder^> /E
echo.
exit /b 1

:do_help
echo MicroRender build and run driver.
echo.
echo   mr build assets ^| dos [mode=raw/tiled/both] [tile=N] [vsync=0/1] ^| pico [preset] [settings] ^| raylib [settings] ^| tests ^| all
echo   mr run dos ^| dosraw [args...]     optimized or raw game in DOSBox
echo   mr run stress ^| stressraw [sprites] [frames]
echo   mr run raylib [args...]            desktop frontend
echo   mr test [index8]                  host suite under ASan/UBSan
echo   mr bench [frames]                 host benchmark
echo   mr clean
echo.
echo Pico presets: game, game-raw, stress-raw, stress-visible, stress-lace,
echo               stress-render, stress-dirtyrect, all
echo.
echo Friendly build settings:
echo   pico:   tile=N sprites=N sys=N spi=N pipeline=ON/OFF mode=NAME
echo           presentation=raw/pipelined hud=N lace=N serial=ON/OFF
echo   dos:    mode=raw/tiled/both tile=N vsync=0/1
echo   raylib: demo=game/stress mode=raw/tiled/lace/dirtyrect tile=N
echo           scale=N sprites=N fps=N autoplay=ON/OFF lace=N
echo           raylib=C:\path\to\raylib
echo   advanced: pass any MR_...=value CMake cache variable through mr build
echo.
echo See the header of mr.bat for environment overrides.
exit /b 0
