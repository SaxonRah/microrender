@echo off
rem MicroRender build driver. Invoked through mr.bat; see there for usage.
setlocal EnableExtensions EnableDelayedExpansion
if "%MR_ROOT%"=="" set "MR_ROOT=%~dp0.."
cd /d "%MR_ROOT%"

if /i "%~1"=="build" shift /1
set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=all"

if /i "%WHAT%"=="assets" goto b_assets
if /i "%WHAT%"=="dos"    goto b_dos
if /i "%WHAT%"=="pico"   goto b_pico
if /i "%WHAT%"=="raylib" goto b_raylib
if /i "%WHAT%"=="tests"  goto b_tests
if /i "%WHAT%"=="all"    goto b_all
echo ERROR: unknown build target "%WHAT%". Try: assets, dos, pico, raylib, tests, all.
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
set "MR_DOS_TILE_H=16"
set "MR_DOS_VSYNC=0"
set "MR_DOS_MODE=both"
shift /1
:dos_opts
if "%~1"=="" goto dos_opts_done
rem CMD treats an unquoted equals sign as a batch-argument delimiter.
rem Therefore both of these must work:
rem   tile=16          arrives as %%1=tile, %%2=16
rem   "tile=16"        arrives as one argument
set "MR_OPT=%~1"
set "MR_KEY=%~1"
set "MR_VALUE=%~2"
set "MR_OPT_ARGC=2"
for /f "tokens=1,* delims==" %%A in ("!MR_OPT!") do (
    if not "%%B"=="" (
        set "MR_KEY=%%A"
        set "MR_VALUE=%%B"
        set "MR_OPT_ARGC=1"
    )
)
if "!MR_VALUE!"=="" (
    echo ERROR: DOS option "!MR_KEY!" has an empty value.
    echo Use either !MR_KEY!=VALUE or "!MR_KEY!=VALUE".
    exit /b 1
)
set "MR_OPT_HANDLED=0"
if /i "!MR_KEY!"=="tile"         (set "MR_DOS_TILE_H=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="tile_h"       (set "MR_DOS_TILE_H=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="vsync"        (set "MR_DOS_VSYNC=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="mode"         (set "MR_DOS_MODE=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="presentation" (set "MR_DOS_MODE=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if "!MR_OPT_HANDLED!"=="0" (
    echo ERROR: unknown DOS build option "!MR_KEY!".
    exit /b 1
)
if "!MR_OPT_ARGC!"=="2" shift /1
shift /1
goto dos_opts
:dos_opts_done
call "%MR_ROOT%\scripts\mr_build.bat" assets
if errorlevel 1 exit /b 1
echo [dos] 320x240 RGB565 logical target, Mode X RGB332 presentation
echo [dos] mode=%MR_DOS_MODE% tile_h=%MR_DOS_TILE_H% vsync_default=%MR_DOS_VSYNC%
cd /d "%MR_ROOT%\microrender_dos"
if /i "%MR_DOS_MODE%"=="both" goto dos_build_both
if /i "%MR_DOS_MODE%"=="raw" goto dos_build_raw
if /i "%MR_DOS_MODE%"=="tiled" goto dos_build_tiled
echo ERROR: DOS mode must be raw, tiled, or both.
exit /b 1

:dos_build_both
call build_watcom.bat dos16 tiled
if errorlevel 1 exit /b 1
call build_watcom_stress.bat dos16 tiled
if errorlevel 1 exit /b 1
call build_watcom.bat dos16 raw
if errorlevel 1 exit /b 1
call build_watcom_stress.bat dos16 raw
if errorlevel 1 exit /b 1
goto dos_stage

:dos_build_raw
call build_watcom.bat dos16 raw
if errorlevel 1 exit /b 1
call build_watcom_stress.bat dos16 raw
if errorlevel 1 exit /b 1
goto dos_stage

:dos_build_tiled
call build_watcom.bat dos16 tiled
if errorlevel 1 exit /b 1
call build_watcom_stress.bat dos16 tiled
if errorlevel 1 exit /b 1

:dos_stage
if not exist "%MR_ROOT%\microrender_dos\dosroot" mkdir "%MR_ROOT%\microrender_dos\dosroot"
for %%F in (mrender.exe mstress.exe mraw.exe msraw.exe) do (
    if exist "dist\%%F" copy /Y "dist\%%F" "dosroot\%%F" >nul
)
echo [dos] ok - optimized and raw binaries staged in microrender_dos\dosroot
exit /b 0

rem ---------------------------------------------------------------------------
:b_pico
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
call "%MR_ROOT%\microrender\pico_env_auto.bat"
if errorlevel 1 exit /b 1

rem The Pico SDK requires a cross-compiler-friendly generator.  Put the SDK's
rem bundled Ninja and ARM GCC first on PATH so CMake cannot silently select the
rem Visual Studio generator/MSVC merely because the command was launched from
rem a Developer PowerShell or a VS Code terminal.
for %%D in ("%NINJA_EXE%") do set "PATH=%%~dpD;%PICO_TOOLCHAIN_PATH%\bin;%PATH%"
set "PICO_SOURCE=%MR_ROOT%\microrender"

set "PRESET=%~2"
if "%PRESET%"=="" set "PRESET=game"
set "PICO_VSCODE=0"
set "EXTRA_FLAGS="
shift /1
shift /1
:pico_opts
if "%~1"=="" goto pico_opts_done
if /i "%~1"=="vscode" (
    set "PICO_VSCODE=1"
) else (
    rem CMD splits unquoted key=value into two batch parameters.  Accept both
    rem the historical unquoted spelling and an explicitly quoted token.
    set "MR_OPT=%~1"
    set "MR_KEY=%~1"
    set "MR_VALUE=%~2"
    set "MR_OPT_ARGC=2"
    for /f "tokens=1,* delims==" %%A in ("!MR_OPT!") do (
        if not "%%B"=="" (
            set "MR_KEY=%%A"
            set "MR_VALUE=%%B"
            set "MR_OPT_ARGC=1"
        )
    )
    if "!MR_VALUE!"=="" (
        echo ERROR: Pico option "!MR_KEY!" has an empty value.
        echo Use either !MR_KEY!=VALUE or "!MR_KEY!=VALUE".
        exit /b 1
    )
    set "MR_OPT_HANDLED=0"
    if /i "!MR_KEY:~0,3!"=="MR_" (
        set "EXTRA_FLAGS=!EXTRA_FLAGS! -D!MR_KEY!=!MR_VALUE!"
        set "MR_OPT_HANDLED=1"
    )
    if /i "!MR_KEY!"=="tile" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_TILE_H=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="tile_h" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_TILE_H=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="sprites" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_STRESS_SPRITES=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="sys" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_PICO_SYS_KHZ=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="spi" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_LCD_SPI_BAUD=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="pipeline" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_PICO_FRAME_PIPELINE=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="mode" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_STRESS_MODE=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="presentation" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_GAME_PRESENTATION=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="hud" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_STRESS_HUD_MODE=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="lace" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_STRESS_LACE_BLOCK_H=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="target" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_STRESS_TARGET_FPS=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="serial" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_STRESS_PICO_SERIAL=!MR_VALUE! -DMR_PICO_GAME_SERIAL=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if /i "!MR_KEY!"=="diag" (set "EXTRA_FLAGS=!EXTRA_FLAGS! -DMR_STRESS_PICO_DIAG=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
    if "!MR_OPT_HANDLED!"=="0" (
        echo ERROR: unknown Pico build option "!MR_KEY!".
        exit /b 1
    )
    if "!MR_OPT_ARGC!"=="2" shift /1
)
shift /1
goto pico_opts
:pico_opts_done

if "%PICO_VSCODE%"=="1" goto b_pico_vscode
if /i "%PRESET%"=="all" goto b_pico_all

call :pico_build_preset "%PRESET%"
if errorlevel 1 exit /b 1
echo [pico] ok - copy the .uf2 to the Pico in BOOTSEL mode
exit /b 0

:b_pico_all
echo [pico] building every Pico preset with Ninja + ARM GCC ...
for %%P in (game game-raw stress-visible stress-raw stress-lace stress-render stress-dirtyrect) do (
    call :pico_build_preset "%%P"
    if errorlevel 1 exit /b 1
)
echo [pico] all presets built successfully
exit /b 0

:b_pico_vscode
if /i "%PRESET%"=="all" (
    echo ERROR: the vscode option needs one preset, not "all".
    exit /b 1
)
set "MR_PY=python"
where python >nul 2>nul || set "MR_PY=py"
set "PRESET_FLAGS="
for /f "delims=" %%F in ('%MR_PY% "%MR_ROOT%\scripts\mr_preset_flags.py" "%PICO_SOURCE%" "%PRESET%"') do set "PRESET_FLAGS=%%F"
if not defined PRESET_FLAGS goto b_pico_badpreset
set "PICO_BUILD_DIR=%PICO_SOURCE%\build"
call :pico_prepare_build_dir "%PICO_BUILD_DIR%"
if errorlevel 1 exit /b 1
echo [pico] configuring preset "%PRESET%" into microrender\build for VS Code ...
cmake -S "%PICO_SOURCE%" -B "%PICO_BUILD_DIR%" -G Ninja ^
    -DCMAKE_MAKE_PROGRAM:FILEPATH="%NINJA_EXE%" %PRESET_FLAGS% %EXTRA_FLAGS%
if errorlevel 1 exit /b 1
cmake --build "%PICO_BUILD_DIR%" --parallel
if errorlevel 1 exit /b 1
echo [pico] ok - microrender\build is now the "%PRESET%" build.
exit /b 0

:pico_build_preset
set "PICO_ONE_PRESET=%~1"
set "MR_PY=python"
where python >nul 2>nul || set "MR_PY=py"
set "PICO_BUILD_DIR="
for /f "delims=" %%D in ('%MR_PY% "%MR_ROOT%\scripts\mr_preset_flags.py" "%PICO_SOURCE%" "%PICO_ONE_PRESET%" --binary-dir') do set "PICO_BUILD_DIR=%%D"
if not defined PICO_BUILD_DIR goto b_pico_badpreset
call :pico_prepare_build_dir "%PICO_BUILD_DIR%"
if errorlevel 1 exit /b 1

echo [pico] configuring preset "%PICO_ONE_PRESET%" %EXTRA_FLAGS% ...
pushd "%PICO_SOURCE%" >nul
if errorlevel 1 (
    echo ERROR: could not enter Pico source directory "%PICO_SOURCE%".
    exit /b 1
)
cmake --preset "%PICO_ONE_PRESET%" ^
    -DCMAKE_MAKE_PROGRAM:FILEPATH="%NINJA_EXE%" %EXTRA_FLAGS%
if errorlevel 1 (
    set "PICO_ERR=!ERRORLEVEL!"
    popd >nul
    exit /b !PICO_ERR!
)
cmake --build --preset "%PICO_ONE_PRESET%" --parallel
if errorlevel 1 (
    set "PICO_ERR=!ERRORLEVEL!"
    popd >nul
    exit /b !PICO_ERR!
)
popd >nul
exit /b 0

:pico_prepare_build_dir
set "PICO_CHECK_DIR=%~1"
if not exist "%PICO_CHECK_DIR%\CMakeCache.txt" exit /b 0
set "PICO_CACHE_BAD=0"
findstr /B /C:"CMAKE_GENERATOR:INTERNAL=Ninja" "%PICO_CHECK_DIR%\CMakeCache.txt" >nul 2>nul
if errorlevel 1 set "PICO_CACHE_BAD=1"
findstr /I /C:"arm-none-eabi-gcc" "%PICO_CHECK_DIR%\CMakeCache.txt" >nul 2>nul
if errorlevel 1 set "PICO_CACHE_BAD=1"
set "PICO_EXPECT_SOURCE=%PICO_SOURCE:\=/%"
set "PICO_EXPECT_BUILD=%PICO_CHECK_DIR:\=/%"
findstr /L /I /X /C:"CMAKE_HOME_DIRECTORY:INTERNAL=!PICO_EXPECT_SOURCE!" "%PICO_CHECK_DIR%\CMakeCache.txt" >nul 2>nul
if errorlevel 1 set "PICO_CACHE_BAD=1"
findstr /L /I /X /C:"CMAKE_CACHEFILE_DIR:INTERNAL=!PICO_EXPECT_BUILD!" "%PICO_CHECK_DIR%\CMakeCache.txt" >nul 2>nul
if errorlevel 1 set "PICO_CACHE_BAD=1"
if "%PICO_CACHE_BAD%"=="1" (
    echo [pico] removing stale or incompatible Pico build directory:
    echo        %PICO_CHECK_DIR%
    rmdir /S /Q "%PICO_CHECK_DIR%"
    if exist "%PICO_CHECK_DIR%" (
        echo ERROR: could not remove stale Pico build directory.
        echo Close Visual Studio, VS Code build tasks, or any terminal using it and retry.
        exit /b 1
    )
)
exit /b 0

:b_pico_badpreset
echo Available presets: game, game-raw, stress-raw, stress-visible, stress-lace,
echo                    stress-render, stress-dirtyrect, all
exit /b 1

rem ---------------------------------------------------------------------------
:b_raylib
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
set "RAY_FLAGS=-DCMAKE_BUILD_TYPE=Release"
set "RAYLIB_OVERRIDE=0"
shift /1
:ray_opts
if "%~1"=="" goto ray_opts_done
rem Accept both unquoted key=value (split by CMD into two parameters) and
rem a quoted "key=value" token.
set "MR_OPT=%~1"
set "MR_KEY=%~1"
set "MR_VALUE=%~2"
set "MR_OPT_ARGC=2"
for /f "tokens=1,* delims==" %%A in ("!MR_OPT!") do (
    if not "%%B"=="" (
        set "MR_KEY=%%A"
        set "MR_VALUE=%%B"
        set "MR_OPT_ARGC=1"
    )
)
if "!MR_VALUE!"=="" (
    echo ERROR: Raylib option "!MR_KEY!" has an empty value.
    echo Use either !MR_KEY!=VALUE or "!MR_KEY!=VALUE".
    exit /b 1
)
set "MR_OPT_HANDLED=0"
if /i "!MR_KEY:~0,3!"=="MR_" (set "RAY_FLAGS=!RAY_FLAGS! -D!MR_KEY!=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="demo" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_DEMO=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="mode" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_MODE=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="tile" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_TILE_H=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="tile_h" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_TILE_H=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="scale" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_SCALE=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="sprites" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_SPRITES=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="fps" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_FPS=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="autoplay" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_AUTOPLAY=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="lace" (set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_DEFAULT_LACE_BLOCK_H=!MR_VALUE!"& set "MR_OPT_HANDLED=1")
if /i "!MR_KEY!"=="raylib" (
    set "RAYLIB_OVERRIDE=1"
    set "RAY_FLAGS=!RAY_FLAGS! -DMR_RAYLIB_PATH:PATH="!MR_VALUE!""
    set "MR_OPT_HANDLED=1"
)
if "!MR_OPT_HANDLED!"=="0" (
    echo ERROR: unknown Raylib build option "!MR_KEY!".
    exit /b 1
)
if "!MR_OPT_ARGC!"=="2" shift /1
shift /1
goto ray_opts
:ray_opts_done
if "%RAYLIB_OVERRIDE%"=="0" (
    call :raylib_ensure_submodule
    if errorlevel 1 exit /b 1
)
echo [raylib] configuring 320x240 RGB565 host frontend ...
cmake -S "%MR_ROOT%\microrender_raylib" -B "%MR_ROOT%\build\raylib" %RAY_FLAGS%
if errorlevel 1 exit /b 1
cmake --build "%MR_ROOT%\build\raylib" --config Release --parallel
if errorlevel 1 exit /b 1
echo [raylib] ok
exit /b 0

:raylib_ensure_submodule
set "RAYLIB_SUBMODULE=%MR_ROOT%\third_party\raylib"
if exist "%RAYLIB_SUBMODULE%\CMakeLists.txt" exit /b 0

echo [raylib] third_party\raylib is not initialized; cloning pinned submodule ...
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: Git is required to initialize the Raylib submodule.
    echo Install Git for Windows, then run:
    echo   git submodule update --init --recursive
    exit /b 1
)
git -C "%MR_ROOT%" rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
    echo ERROR: this source tree is not a Git checkout, so the Raylib submodule cannot be initialized.
    echo Clone with:
    echo   git clone --recurse-submodules https://github.com/SaxonRah/microrender.git
    exit /b 1
)
git -C "%MR_ROOT%" submodule update --init --recursive --depth 1 -- third_party/raylib
if errorlevel 1 (
    echo [raylib] shallow submodule update failed; retrying full pinned checkout ...
    git -C "%MR_ROOT%" submodule update --init --recursive -- third_party/raylib
)
if errorlevel 1 (
    echo ERROR: could not initialize third_party\raylib.
    echo Retry manually with:
    echo   git submodule sync --recursive
    echo   git submodule update --init --recursive
    exit /b 1
)
if not exist "%RAYLIB_SUBMODULE%\CMakeLists.txt" (
    echo ERROR: Raylib submodule initialized without CMakeLists.txt.
    exit /b 1
)
echo [raylib] submodule ready
exit /b 0

rem ---------------------------------------------------------------------------
:b_tests
call "%MR_ROOT%\scripts\mr_tools.bat" cmake
if errorlevel 1 exit /b 1
set "PRESET=debug"
if /i "%~2"=="index8" set "PRESET=index8"
echo [tests] configuring preset "%PRESET%" ...
pushd "%MR_ROOT%\tests" >nul
if errorlevel 1 exit /b 1
cmake --preset "%PRESET%"
if errorlevel 1 (
    set "TEST_ERR=!ERRORLEVEL!"
    popd >nul
    exit /b !TEST_ERR!
)
cmake --build --preset "%PRESET%" --parallel
if errorlevel 1 (
    set "TEST_ERR=!ERRORLEVEL!"
    popd >nul
    exit /b !TEST_ERR!
)
popd >nul
echo [tests] ok
exit /b 0

rem ---------------------------------------------------------------------------
:b_all
set "MR_ALL_FAILED=0"
call "%MR_ROOT%\scripts\mr_build.bat" assets
if errorlevel 1 exit /b 1

call "%MR_ROOT%\scripts\mr_build.bat" tests
if errorlevel 1 (
    echo [all] host tests failed - continuing with remaining targets
    set "MR_ALL_FAILED=1"
)

if not "%WATCOM%"=="" (
    call "%MR_ROOT%\scripts\mr_build.bat" dos
    if errorlevel 1 (
        echo [all] DOS build failed
        set "MR_ALL_FAILED=1"
    )
) else (
    echo [all] WATCOM not set - skipping DOS target
)

cmake --version >nul 2>nul
if errorlevel 1 (
    echo [all] CMake not available - Pico and Raylib were not built
    set "MR_ALL_FAILED=1"
) else (
    call "%MR_ROOT%\scripts\mr_build.bat" pico all
    if errorlevel 1 (
        echo [all] one or more Pico builds failed or SDK not present
        set "MR_ALL_FAILED=1"
    )
    call "%MR_ROOT%\scripts\mr_build.bat" raylib
    if errorlevel 1 (
        echo [all] Raylib build failed
        set "MR_ALL_FAILED=1"
    )
)

if "%MR_ALL_FAILED%"=="1" (
    echo [all] completed with one or more failures
    exit /b 1
)
echo [all] all available targets built successfully
exit /b 0
