@echo off
REM Встановлення Сокіл для Windows
REM Використання: install.bat
echo Збираємо Сокіл...
gcc -O2 -std=c99 -lm -o sokil.exe sokil.c
if errorlevel 1 (
    echo Потрібен gcc (MinGW-w64): winget install BrechtSanders.WinLibs.MCF.UCRT
    exit /b 1
)

echo Копіюємо sokil.exe у %LOCALAPPDATA%\Sokil...
set TARGET=%LOCALAPPDATA%\Sokil
if not exist "%TARGET%" mkdir "%TARGET%"
copy /y sokil.exe "%TARGET%\sokil.exe" >nul

echo Додаємо у PATH...
setx PATH "%TARGET%;%PATH%" >nul

echo Готово! Встановлено у %TARGET%
echo Запуск: sokil (REPL) або sokil файл.sokil
echo Важливо: відкрий НОВИЙ термінал, щоб PATH оновився