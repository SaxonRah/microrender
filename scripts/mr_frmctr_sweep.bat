@echo off
rem ---------------------------------------------------------------------------
rem Build one stress-lace image per FRMCTR1 candidate so the panel refresh rate
rem can be swept by eye.
rem
rem The panel scans its own GRAM independently of how fast we present to it. At
rem the ILI9341 reset default that is about 70 Hz, so a board presenting 110 FPS
rem has roughly 40 of every 110 frames overwritten before they are displayed.
rem Raising FRMCTR1 is the only change that alters what is actually visible.
rem
rem It cannot be set to the datasheet maximum and left there. Fewer clocks per
rem line means less time to charge each row: past some point the crystal never
rem fully switches and the display washes out to near-white with the image
rem faint behind it. Measured on a Pimoroni Pico Plus 2 with a generic ILI9341,
rem RTNA=0x10 (119 Hz) does exactly that. The usable value is per module.
rem
rem The serial FPS will read the same for every build here. That is expected --
rem this changes the panel, not the presentation rate. Judge it by eye, and
rem compare against the 0x1B baseline rather than looking at one build alone.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions EnableDelayedExpansion

set "MR_ROOT=%~dp0.."
pushd "%MR_ROOT%" >nul || (echo ERROR: cannot enter repo root & exit /b 1)

set "SWEEP=0x1B 0x19 0x18 0x16 0x13 0x10"
set "OUTDIR=%MR_ROOT%\build-frmctr-sweep"
if not exist "%OUTDIR%" mkdir "%OUTDIR%" >nul 2>nul

for %%R in (%SWEEP%) do (
    echo.
    echo ============================================================
    echo [sweep] building RTNA=%%R
    echo ============================================================
    call "%MR_ROOT%\mr.bat" build pico stress-lace MR_ILI9341_FRMCTR1_RTNA=%%R serial=ON
    if errorlevel 1 (
        echo ERROR: build failed for RTNA=%%R
        popd >nul
        exit /b 1
    )
    for /f "delims=" %%U in ('dir /s /b "%MR_ROOT%\microrender\build-stress-lace\*.uf2" 2^>nul') do (
        copy /Y "%%U" "%OUTDIR%\stress-lace-rtna-%%R.uf2" >nul
    )
)

echo.
echo ============================================================
echo [sweep] done. UF2 images in:
echo     %OUTDIR%
echo.
echo Flash them in order, starting with 0x1B as your reference.
echo Take the fastest value that still shows solid blacks and
echo full-brightness whites, then step back one for margin.
echo Contrast degrades gradually rather than failing outright,
echo so compare against 0x1B rather than judging one alone.
echo ============================================================
popd >nul
exit /b 0
