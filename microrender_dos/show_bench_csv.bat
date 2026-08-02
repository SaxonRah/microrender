@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not exist "dosroot\BENCH2.CSV" (
    echo No dosroot\BENCH2.CSV found yet.
    echo Run a /benchsummary runner first.
    exit /b 1
)
type "dosroot\BENCH2.CSV"
endlocal
