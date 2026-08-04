@echo off
rem Standalone MicroRender RLE/collision stress-test Open Watcom DOS build.
setlocal EnableExtensions
cd /d "%~dp0"

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=dos16"
if /I "%TARGET%"=="clean" goto clean
if /I "%TARGET%"=="rebuild" (
    call "%~f0" clean
    if errorlevel 1 goto fail
    if "%~2"=="" (
        call "%~f0" dos16 tiled
    ) else (
        call "%~f0" dos16 "%~2"
    )
    exit /b %ERRORLEVEL%
)
if /I not "%TARGET%"=="dos16" if /I not "%TARGET%"=="dos" (
  echo ERROR: unknown target "%TARGET%"
  echo Use: dos16 [raw|tiled], dos [raw|tiled], clean, rebuild [raw|tiled]
  exit /b 1
)

set "MODE=%~2"
if "%MODE%"=="" set "MODE=tiled"
if /I "%MODE%"=="raw" (
    set "PRESENT_ID=0"
    set "MODE_TAG=raw"
) else if /I "%MODE%"=="tiled" (
    set "PRESENT_ID=1"
    set "MODE_TAG=tiled"
) else (
    echo ERROR: presentation mode must be raw or tiled, got "%MODE%"
    exit /b 1
)

call :check_watcom
if errorlevel 1 goto fail
call :make_dirs
if errorlevel 1 goto fail

set "OBJDIR=build\obj\stress_dos16_%MODE_TAG%"
set "ERRDIR=build\err\stress_dos16_%MODE_TAG%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%ERRDIR%" mkdir "%ERRDIR%"
if /I "%MODE_TAG%"=="raw" (set "EXE=dist\msraw.exe") else (set "EXE=dist\mstress.exe")

rem DOS stress test uses the same 320x240 RGB565 logical target.
rem This mirrors the working build_watcom.bat setup but adds mr_stress_test.c
rem and links dos\dos_stress_app.c as the entry program.
set "CFLAGS=-q -bt=dos -ml -2 -ox -s -w4 -dGFX_FIXED_NO_INT64 -dGFX_COLOR_INDEX8=0 -dGFX_ENABLE_TRIANGLES=1 -dMR_STRESS_MAX_SPRITES=1024 -i=..\shared\src -i=dos"
if "%MR_DOS_TILE_H%"=="" set "MR_DOS_TILE_H=16"
if "%MR_DOS_VSYNC%"=="" set "MR_DOS_VSYNC=0"
set "CFLAGS=%CFLAGS% -dMR_DOS_TILE_H=%MR_DOS_TILE_H% -dMR_DOS_VSYNC=%MR_DOS_VSYNC% -dMR_DOS_PRESENT_MODE=%PRESENT_ID%"
set "LFLAGS=-q -bt=dos -ml"

echo Building DOS MicroRender RLE/collision stress target [%MODE_TAG%] from ..\shared\src...
echo WATCOM: %WATCOM%
echo Compiler: %OW_WCC%
echo Linker: %OW_WCL%

call :compile ..\shared\src\gfx.c "%OBJDIR%\gfx.obj" "%ERRDIR%\gfx.err"
if errorlevel 1 goto fail
call :compile ..\shared\src\gfx_font5x7.c "%OBJDIR%\gfx_font5x7.obj" "%ERRDIR%\gfx_font5x7.err"
if errorlevel 1 goto fail
call :compile ..\shared\src\gfx_triangle.c "%OBJDIR%\gfx_triangle.obj" "%ERRDIR%\gfx_triangle.err"
if errorlevel 1 goto fail
call :compile ..\shared\src\gfx_engine.c "%OBJDIR%\gfx_engine.obj" "%ERRDIR%\gfx_engine.err"
if errorlevel 1 goto fail
call :compile ..\shared\src\mr_stress_test.c "%OBJDIR%\mr_stress_test.obj" "%ERRDIR%\mr_stress_test.err"
if errorlevel 1 goto fail
call :compile ..\shared\src\mr_strbuf.c "%OBJDIR%\mr_strbuf.obj" "%ERRDIR%\mr_strbuf.err"
if errorlevel 1 goto fail
call :compile dos\dos_vga.c "%OBJDIR%\dos_vga.obj" "%ERRDIR%\dos_vga.err"
if errorlevel 1 goto fail
call :compile dos\dos_stress_app.c "%OBJDIR%\dos_stress_app.obj" "%ERRDIR%\dos_stress_app.err"
if errorlevel 1 goto fail

echo Linking %EXE%...
"%OW_WCL%" %LFLAGS% -fe="%EXE%" ^
  "%OBJDIR%\dos_stress_app.obj" ^
  "%OBJDIR%\gfx.obj" ^
  "%OBJDIR%\gfx_font5x7.obj" ^
  "%OBJDIR%\gfx_triangle.obj" ^
  "%OBJDIR%\gfx_engine.obj" ^
  "%OBJDIR%\mr_strbuf.obj" ^
  "%OBJDIR%\dos_vga.obj" ^
  "%OBJDIR%\mr_stress_test.obj" > "%ERRDIR%\link.err" 2>&1
if errorlevel 1 (
  type "%ERRDIR%\link.err"
  echo ERROR: link failed.
  goto fail
)

echo.
echo Build OK: %EXE%
echo Try: dist\mstress.exe /sprites 512 /frames 2100 /novsync
echo Try: dist\mstress.exe /sprites 1024 /frames 2100 /novsync
goto done

:check_watcom
if "%WATCOM%"=="" set "WATCOM=C:\WATCOM"
if exist "%WATCOM%\BINNT\wcc.exe" (
  set "OW_WCC=%WATCOM%\BINNT\wcc.exe"
) else if exist "%WATCOM%\BINW\wcc.exe" (
  set "OW_WCC=%WATCOM%\BINW\wcc.exe"
) else (
  echo ERROR: wcc.exe not found under "%WATCOM%".
  exit /b 1
)
if exist "%WATCOM%\BINNT\wcl.exe" (
  set "OW_WCL=%WATCOM%\BINNT\wcl.exe"
) else if exist "%WATCOM%\BINW\wcl.exe" (
  set "OW_WCL=%WATCOM%\BINW\wcl.exe"
) else (
  echo ERROR: wcl.exe not found under "%WATCOM%".
  exit /b 1
)
set "PATH=%WATCOM%\BINNT;%WATCOM%\BINW;%PATH%"
set "INCLUDE=%WATCOM%\H;%WATCOM%\H\NT;%INCLUDE%"
set "EDPATH=%WATCOM%\EDDAT"
exit /b 0

:make_dirs
if not exist build mkdir build
if not exist build\obj mkdir build\obj
if not exist build\err mkdir build\err
if not exist dist mkdir dist
exit /b 0

:compile
set "SRC=%~1"
set "OBJ=%~2"
set "ERR=%~3"
echo Compiling %SRC%...
"%OW_WCC%" %CFLAGS% -fo="%OBJ%" "%SRC%" > "%ERR%" 2>&1
if errorlevel 1 (
  type "%ERR%"
  echo ERROR: compile failed: %SRC%
  exit /b 1
)
exit /b 0

:clean
cd /d "%~dp0"
echo Cleaning MicroRender DOS stress build output...
if exist build\obj\stress_dos16_tiled rmdir /s /q build\obj\stress_dos16_tiled
if exist build\obj\stress_dos16_raw rmdir /s /q build\obj\stress_dos16_raw
if exist build\err\stress_dos16_tiled rmdir /s /q build\err\stress_dos16_tiled
if exist build\err\stress_dos16_raw rmdir /s /q build\err\stress_dos16_raw
if exist dist\mstress.exe del /q dist\mstress.exe
if exist dist\msraw.exe del /q dist\msraw.exe
echo Clean OK.
goto done

:fail
echo.
echo Build FAILED.
echo Check build\err\stress_dos16 for compiler/linker error files if they were created.
exit /b 1

:done
endlocal
