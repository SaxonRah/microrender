@echo off
rem ---------------------------------------------------------------------------
rem Run the Raylib frontend across every demo/mode/tile combination for a fixed
rem number of frames, and report which ones exit cleanly.
rem
rem This is a smoke test, not a correctness test. The host unit, game and fuzz
rem suites are what actually check rendering output; this checks that every
rem combination of options still starts, runs, and exits 0 -- which is the
rem failure the unit tests cannot see, because they never construct a frontend.
rem
rem Two modes:
rem
rem   mr_test_raylib.bat [frames]
rem       Unattended smoke test. Each combination runs [frames] frames
rem       uncapped and exits on its own. Fast, and useful for checking that
rem       nothing crashes, but far too quick to look at.
rem
rem   mr_test_raylib.bat watch [fps]
rem       Visual check. Each combination opens with no frame limit and stays
rem       open until you close the window; closing it advances to the next.
rem       Defaults to a 60 FPS cap so motion is watchable.
rem
rem       Pass 0 for fps to run uncapped. Since the simulation is on a fixed
rem       timestep, an uncapped window should move at exactly the same speed as
rem       a capped one -- running the same case both ways is the most direct
rem       check that frame rate and simulation rate are actually separated.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions EnableDelayedExpansion

set "MR_ROOT=%~dp0.."
pushd "%MR_ROOT%" >nul || (echo ERROR: cannot enter repo root & exit /b 1)

set "MODE=smoke"
set "FRAMES=120"
set "CAP=60"
if /i "%~1"=="watch" (
    set "MODE=watch"
    if not "%~2"=="" set "CAP=%~2"
) else (
    if not "%~1"=="" set "FRAMES=%~1"
)

set "PASS=0"
set "FAIL=0"
set "FAILED="

if /i "%MODE%"=="watch" (
    echo [raylib] watch mode: close each window to advance to the next case.
    if "%CAP%"=="0" (
        echo [raylib] uncapped -- speed should match the capped run exactly.
    ) else (
        echo [raylib] capped at %CAP% FPS.
    )
    echo.
)

echo [raylib] building frontend ...
call "%MR_ROOT%\mr.bat" build raylib
if errorlevel 1 (
    echo ERROR: raylib build failed.
    popd >nul
    exit /b 1
)

call :run "game  raw"          --demo game   --mode raw
call :run "game  tiled t8"     --demo game   --mode tiled  --tile 8
call :run "game  tiled t16"    --demo game   --mode tiled  --tile 16
call :run "game  tiled t240"   --demo game   --mode tiled  --tile 240
call :run "game  autoplay"     --demo game   --mode tiled  --tile 16 --autoplay ON

call :run "strs  raw"          --demo stress --mode raw
call :run "strs  visible"      --demo stress --mode visible --sprites 512
call :run "strs  lace b4"      --demo stress --mode lace    --sprites 1024 --lace-block 4
call :run "strs  lace b8"      --demo stress --mode lace    --sprites 1024 --lace-block 8
call :run "strs  lace b60"     --demo stress --mode lace    --sprites 1024 --lace-block 60
call :run "strs  render"       --demo stress --mode render  --sprites 1024
call :run "strs  dirtyrect"    --demo stress --mode dirtyrect --sprites 512 --tile 16
call :run "strs  1 sprite"     --demo stress --mode visible --sprites 1
call :run "strs  max sprites"  --demo stress --mode visible --sprites 2048

echo.
echo ============================================================
echo [raylib] passed !PASS!, failed !FAIL!
if not "!FAILED!"=="" echo [raylib] failures:!FAILED!
echo ============================================================
popd >nul
if !FAIL! GTR 0 exit /b 1
exit /b 0

:run
set "LABEL=%~1"
shift /1
set "ARGS="
:run_collect
if "%~1"=="" goto run_go
set "ARGS=!ARGS! %~1"
shift /1
goto run_collect
:run_go
if /i "%MODE%"=="watch" goto run_watch

echo [raylib] !LABEL!
call "%MR_ROOT%\mr.bat" run raylib !ARGS! --frames %FRAMES% --fps 0 >nul 2>&1
if errorlevel 1 (
    echo          FAILED ^(exit !ERRORLEVEL!^)
    set /a FAIL+=1
    set "FAILED=!FAILED! [!LABEL!]"
) else (
    set /a PASS+=1
)
exit /b 0

:run_watch
rem No --frames, so the window stays up until closed. Output is not redirected
rem here: in watch mode you want to see whatever the frontend prints.
echo.
echo ------------------------------------------------------------
echo [raylib] !LABEL!      ^(close the window to continue^)
echo ------------------------------------------------------------
call "%MR_ROOT%\mr.bat" run raylib !ARGS! --fps %CAP%
if errorlevel 1 (
    echo          FAILED ^(exit !ERRORLEVEL!^)
    set /a FAIL+=1
    set "FAILED=!FAILED! [!LABEL!]"
) else (
    set /a PASS+=1
)
exit /b 0
