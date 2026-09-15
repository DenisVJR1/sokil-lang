@echo off
REM Збірка Сокіл для Windows
if not exist dist mkdir dist
gcc -O2 -std=c99 -lm -o dist\sokil.exe sokil.c
if errorlevel 1 (
    echo Потрібен gcc (MinGW-w64): winget install BrechtSanders.WinLibs.MCF.UCRT
    exit /b 1
)
echo Готово: dist\sokil.exe
echo Запуск: dist\sokil.exe файл.sokil