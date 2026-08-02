@echo off
cd /d "%~dp0.."
if not exist mr_asset.exe call build_mr_asset_gcc.bat
if not exist mr_asset.exe (
  echo ERROR: mr_asset.exe was not built.
  exit /b 1
)
mr_asset.exe examples\checker8.bmp checker --raw
mr_asset.exe examples\masked8.bmp masked --rle --key 0
if exist checker.h move /y checker.h examples\checker8_sprite.h >nul
if exist masked.h move /y masked.h examples\masked8_sprite.h >nul
echo Wrote examples\checker8_sprite.h and examples\masked8_sprite.h
