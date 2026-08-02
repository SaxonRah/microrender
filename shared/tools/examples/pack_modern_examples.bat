@echo off
cd /d "%~dp0\.."
python mr_pack.py --bmp-rle checker examples\checker8.bmp --bmp-rle masked examples\masked8.bmp --tiled level examples\tiled_demo.json -o examples\demo.mrp --header examples\demo_mrp.h --symbol demo_mrp
if errorlevel 1 exit /b 1
echo Wrote tools\examples\demo.mrp and demo_mrp.h
