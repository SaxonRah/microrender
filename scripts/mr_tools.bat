@echo off
rem Locate the external toolchains once, so no other script hardcodes a path.
rem Call with the tool name; sets the matching variable in the caller's scope.
rem   call mr_tools.bat dosbox   -> MR_DOSBOX
rem   call mr_tools.bat watcom   -> MR_WATCOM
rem   call mr_tools.bat cmake    -> MR_CMAKE
rem Returns 1 if the tool could not be found.

if /i "%~1"=="dosbox" goto find_dosbox
if /i "%~1"=="watcom" goto find_watcom
if /i "%~1"=="cmake"  goto find_cmake
echo mr_tools.bat: unknown tool "%~1"
exit /b 1

rem ---------------------------------------------------------------------------
:find_dosbox
if not "%DOSBOX_EXE%"=="" (
    if exist "%DOSBOX_EXE%" set "MR_DOSBOX=%DOSBOX_EXE%" & exit /b 0
    echo WARNING: DOSBOX_EXE is set but "%DOSBOX_EXE%" does not exist.
)
set "MR_DOSBOX="
for %%E in (dosbox-x.exe dosbox.exe DOSBox.exe) do (
    if not defined MR_DOSBOX (
        for /f "delims=" %%P in ('where %%E 2^>nul') do (
            if not defined MR_DOSBOX set "MR_DOSBOX=%%P"
        )
    )
)
for %%D in (
    "%ProgramFiles%\DOSBox-X\dosbox-x.exe"
    "%ProgramFiles(x86)%\DOSBox-X\dosbox-x.exe"
    "%ProgramFiles%\DOSBox-0.74-3\DOSBox.exe"
    "%ProgramFiles(x86)%\DOSBox-0.74-3\DOSBox.exe"
    "%ProgramFiles%\DOSBox-0.74\DOSBox.exe"
    "%ProgramFiles(x86)%\DOSBox-0.74\DOSBox.exe"
) do (
    if not defined MR_DOSBOX if exist %%D set "MR_DOSBOX=%%~D"
)
if not defined MR_DOSBOX (
    echo ERROR: DOSBox not found on PATH or in the usual install folders.
    echo Set DOSBOX_EXE to the full path of dosbox-x.exe or dosbox.exe.
    exit /b 1
)
exit /b 0

rem ---------------------------------------------------------------------------
:find_watcom
if "%WATCOM%"=="" (
    echo ERROR: WATCOM is not set. Point it at your Open Watcom install root.
    exit /b 1
)
if exist "%WATCOM%\binnt\wcc.exe" (
    set "MR_WATCOM=%WATCOM%"
    exit /b 0
)
if exist "%WATCOM%\binw\wcc.exe" (
    set "MR_WATCOM=%WATCOM%"
    exit /b 0
)
echo ERROR: wcc.exe not found under "%WATCOM%" (looked in binnt\ and binw\).
exit /b 1

rem ---------------------------------------------------------------------------
:find_cmake
set "MR_CMAKE="
for /f "delims=" %%P in ('where cmake 2^>nul') do (
    if not defined MR_CMAKE set "MR_CMAKE=%%P"
)
if not defined MR_CMAKE (
    echo ERROR: cmake not found on PATH.
    exit /b 1
)
exit /b 0
