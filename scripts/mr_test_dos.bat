@echo off
rem ---------------------------------------------------------------------------
rem Build the 16-bit DOS frontend across its option matrix, and optionally run
rem each binary in DOSBox.
rem
rem The DOS target is the one most likely to break silently. It is the only
rem build with 16-bit int and far pointers, so shared code that compiles and
rem passes tests everywhere else can still fail here -- pointer arithmetic that
rem assumes a flat 32-bit address space, or an int that quietly overflows at
rem 32,767. Nothing in CI covers it, because Open Watcom is not installed there.
rem
rem Build-only by default, since running needs DOSBox and is not unattended.
rem Pass "run" to also launch each binary with /auto.
rem
rem Usage:  mr_test_dos.bat [run]
rem ---------------------------------------------------------------------------
setlocal EnableExtensions EnableDelayedExpansion

set "MR_ROOT=%~dp0.."
pushd "%MR_ROOT%" >nul || (echo ERROR: cannot enter repo root & exit /b 1)

set "DO_RUN=0"
if /i "%~1"=="run" set "DO_RUN=1"

if not defined WATCOM (
    echo ERROR: WATCOM is not set. The DOS target needs Open Watcom.
    echo        set WATCOM=C:\WATCOM
    popd >nul
    exit /b 1
)

set "PASS=0"
set "FAIL=0"
set "FAILED="

call :build "raw   t16 vsync0"  mode=raw   tile=16 vsync=0
call :build "raw   t16 vsync1"  mode=raw   tile=16 vsync=1
call :build "tiled t8  vsync0"  mode=tiled tile=8  vsync=0
call :build "tiled t16 vsync0"  mode=tiled tile=16 vsync=0
call :build "tiled t16 vsync1"  mode=tiled tile=16 vsync=1
call :build "tiled t24 vsync0"  mode=tiled tile=24 vsync=0
call :build "both  t16 vsync0"  mode=both  tile=16 vsync=0

echo.
echo ============================================================
echo [dos] builds passed !PASS!, failed !FAIL!
if not "!FAILED!"=="" echo [dos] failures:!FAILED!
echo ============================================================

if "%DO_RUN%"=="0" (
    echo.
    echo [dos] build-only. Pass "run" to launch each binary in DOSBox:
    echo           .\scripts\mr_test_dos.bat run
    popd >nul
    if !FAIL! GTR 0 exit /b 1
    exit /b 0
)

echo.
echo [dos] launching in DOSBox. Each window runs /auto and exits on its own.
echo       Watch for corrupted tiles, wrong colours, or an early exit.
echo.

rem Rebuild "both" so mrender.exe and mraw.exe are the binaries being run.
call "%MR_ROOT%\mr.bat" build dos mode=both tile=16 vsync=0
if errorlevel 1 (
    echo ERROR: could not rebuild for the run pass.
    popd >nul
    exit /b 1
)

echo [dos] game, tiled
call "%MR_ROOT%\mr.bat" run dos /auto
echo [dos] game, raw
call "%MR_ROOT%\mr.bat" run dosraw /auto
rem stress takes [sprites] [frames]; a frame count makes it exit by itself.
echo [dos] stress, 512 sprites
call "%MR_ROOT%\mr.bat" run stress 512 2100
echo [dos] stress raw, 512 sprites
call "%MR_ROOT%\mr.bat" run stressraw 512 2100

popd >nul
if !FAIL! GTR 0 exit /b 1
exit /b 0

:build
set "LABEL=%~1"
shift /1
set "ARGS="
:build_collect
if "%~1"=="" goto build_go
set "ARGS=!ARGS! %~1"
shift /1
goto build_collect
:build_go
echo [dos] building !LABEL!
call "%MR_ROOT%\mr.bat" build dos !ARGS! >nul 2>&1
if errorlevel 1 (
    echo          FAILED ^(exit !ERRORLEVEL!^) - rerun without redirection to see why:
    echo          .\mr.bat build dos!ARGS!
    set /a FAIL+=1
    set "FAILED=!FAILED! [!LABEL!]"
) else (
    set /a PASS+=1
)
exit /b 0
