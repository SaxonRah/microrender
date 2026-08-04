@echo off
rem Remove every build artifact. Source and committed generated assets are left
rem alone; regenerate those with "mr build assets".
setlocal EnableExtensions
if "%MR_ROOT%"=="" set "MR_ROOT=%~dp0.."
cd /d "%MR_ROOT%"

for %%D in (
    "build"
    "microrender\build"
    "microrender\build-game-raw"
    "microrender\build-stress-raw"
    "microrender\build-stress-visible"
    "microrender\build-stress-lace"
    "microrender\build-stress-render"
    "microrender\build-stress-dirtyrect"
    "microrender_dos\build"
    "microrender_dos\obj"
    "microrender_dos\dist"
    "microrender_dos\dosroot"
) do (
    if exist %%D (
        echo removing %%D
        rmdir /s /q %%D
    )
)
echo clean: done
exit /b 0
