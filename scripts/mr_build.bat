@echo off
rem MicroRender build driver. Invoked through mr.bat; see there for usage.
setlocal EnableExtensions
if "%MR_ROOT%"=="" set "MR_ROOT=%~dp0.."
cd /d "%MR_ROOT%"

set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=all"

if /i "%WHAT%"=="assets" goto b_assets
if /i "%WHAT%"=="dos"    goto b_dos
if /i "%WHAT%"=="pico"   goto b_pico
if /i "%WHAT%"=="tests"  goto b_tests
if /i "%WHAT%"=="all"    goto b_all
echo ERROR: unknown build target "%WHAT%". Try: assets, dos, pico, tests, all.
exit /b 1

rem ---------------------------------------------------------------------------
:b_assets
set "MR_PY=python"
where python >nul 2>nul || set "MR_PY=py"
where %MR_PY% >nul 2>nul
if errorlevel 1 (
    echo ERROR: no Python interpreter found on PATH ^(tried python, py^).
    exit /b 1
)
if not exist "%MR_ROOT%\shared\tools\mr_pack.py" (
    echo ERROR: "%MR_ROOT%\shared\tools\mr_pack.py" not found.
    echo Run mr.bat from the repository root, not from an unpacked patch bundle.
    exit /b 1
)
if not exist "%MR_ROOT%\shared\generated" mkdir "%MR_ROOT%\shared\generated"
echo [assets] regenerating pack from shared\assets ...
%MR_PY% "%MR_ROOT%\shared\tools\mr_pack.py" ^
    --manifest "%MR_ROOT%\shared\assets\project.json" ^
    -o "%MR_ROOT%\shared\generated\GAME.MRP" ^
    --header "%MR_ROOT%\shared\generated\game_mrp.h" ^
    --symbol game_mrp
if errorlevel 1 exit /b 1
%MR_PY% "%MR_ROOT%\shared\tools\mr_embed.py" ^
    --input "%MR_ROOT%\shared\generated\GAME.MRP" ^
    --c "%MR_ROOT%\shared\generated\mr_embedded_assets.c" ^
    --h "%MR_ROOT%\shared\generated\mr_embedded_assets.h" ^
    --symbol mr_embedded_assets
if errorlevel 1 exit /b 1
echo [assets] ok
exit /b 0

rem ---------------------------------------------------------------------------
:b_dos
call "%MR_ROOT%\scripts\mr_tools.bat" watcom
if errorlevel 1 exit /b 1
call "%MR_ROOT%\scripts\mr_build.bat" assets
if errorlevel 1 exit /b 1
echo [dos] building mrender.exe and mstress.exe with Open Watcom ...
cd /d "%MR_ROOT%\microrender_dos"
call build_watcom.bat dos16
if errorlevel 1 exit /b 1
call build_watcom_stress.bat
if errorlevel 1 exit /b 1
if not exist "%MR_ROOT%\microrender_dos\dosroot" mkdir "%MR_ROOT%\microrender_dos\dosroot"
if exist dist\mrender.exe copy /Y dist\mrender.exe dosroot\mrender.exe >nul
if exist dist\mstress.exe copy /Y dist\mstress.exe dosroot\mstress.exe >nul
echo [dos] ok - binaries staged in microrender_dos\dosroot
exit /b 0

rem ---------------------------------------------------------------------------
:b_pico
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
set "PRESET=%~2"
if "%PRESET%"=="" set "PRESET=game"

rem "vscode" as the third argument configures into microrender\build instead of
rem the preset's own directory. Every task in microrender\.vscode\tasks.json and
rem the Cortex-Debug launch config are hardcoded to ${workspaceFolder}/build, so
rem this is what makes the extension's Flash and Debug buttons pick up the
rem variant you actually asked for rather than whatever was in build\ before.
if /i "%~3"=="vscode" goto b_pico_vscode

echo [pico] configuring preset "%PRESET%" ...
cmake --preset "%PRESET%" -S "%MR_ROOT%\microrender"
if errorlevel 1 goto b_pico_badpreset
cmake --build --preset "%PRESET%"
if errorlevel 1 exit /b 1
echo [pico] ok - copy the .uf2 to the Pico in BOOTSEL mode
exit /b 0

:b_pico_vscode
set "MR_PY=python"
where python >nul 2>nul || set "MR_PY=py"
for /f "delims=" %%F in ('%MR_PY% "%MR_ROOT%\scripts\mr_preset_flags.py" "%MR_ROOT%\microrender" "%PRESET%"') do set "PRESET_FLAGS=%%F"
if not defined PRESET_FLAGS goto b_pico_badpreset
echo [pico] configuring preset "%PRESET%" into microrender\build for VS Code ...
echo [pico] %PRESET_FLAGS%
cmake -S "%MR_ROOT%\microrender" -B "%MR_ROOT%\microrender\build" -G Ninja %PRESET_FLAGS%
if errorlevel 1 exit /b 1
cmake --build "%MR_ROOT%\microrender\build"
if errorlevel 1 exit /b 1
echo.
echo [pico] ok - microrender\build is now the "%PRESET%" build.
echo        In VS Code: Ctrl+Shift+P then "Tasks: Run Task" ^> Flash
echo        or press F5 to flash and debug through the probe.
exit /b 0

:b_pico_badpreset
echo.
echo Available presets: game, stress-visible, stress-lace, stress-render,
echo                    stress-dirtyrect
exit /b 1

rem ---------------------------------------------------------------------------
:b_tests
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
set "PRESET=debug"
if /i "%~2"=="index8" set "PRESET=index8"
echo [tests] configuring preset "%PRESET%" ...
cmake --preset "%PRESET%" -S "%MR_ROOT%\tests"
if errorlevel 1 exit /b 1
cmake --build --preset "%PRESET%"
if errorlevel 1 exit /b 1
echo [tests] ok
exit /b 0

rem ---------------------------------------------------------------------------
:b_all
call "%MR_ROOT%\scripts\mr_build.bat" assets
if errorlevel 1 exit /b 1

call "%MR_ROOT%\scripts\mr_build.bat" tests
if errorlevel 1 echo [all] host tests skipped or failed - continuing

rem DOS and Pico need toolchains that may not be installed. Report and carry on
rem rather than failing the whole run.
if not "%WATCOM%"=="" (
    call "%MR_ROOT%\scripts\mr_build.bat" dos
    if errorlevel 1 echo [all] DOS build failed
) else (
    echo [all] WATCOM not set - skipping DOS target
)

cmake --version >nul 2>nul
if not errorlevel 1 (
    call "%MR_ROOT%\scripts\mr_build.bat" pico game
    if errorlevel 1 echo [all] Pico build failed or SDK not present
)
echo [all] done
exit /b 0
