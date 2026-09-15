@echo off
REM Встановлення Сокіл для Windows
REM Використання: install.bat
echo Zbiraemo Sokil...
gcc -O2 -std=c99 -o sokil.exe sokil.c
if errorlevel 1 (
    echo Potriben gcc (MinGW-w64). Vstanovite: winget install BrechtSanders.WinLibs.MCF.UCRT
    exit /b 1
)

echo Kopiyuemo sokil.exe u shlyah...
set TARGET=%LOCALAPPDATA%\Sokil
if not exist "%TARGET%" mkdir "%TARGET%"
copy /y sokil.exe "%TARGET%\sokil.exe" >nul

REM Dodayemo u PATH (na postiyno)
setx PATH "%TARGET%;%PATH%" >nul

echo Gotovo! Vstanovleno u %TARGET%
echo Zapusk: sokil (REPL) abo sokil fail.sokil
echo Uvaga: vidkryi NOVYY terminal, shchob PATH onovyvsya