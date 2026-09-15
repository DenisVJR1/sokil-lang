@echo off
REM Збірка бінарника для Windows
echo Зbirka Sokil dlya Windows...
pip install pyinstaller
pyinstaller --onefile --name sokil.exe sokil.py
echo Gotovo: dist\sokil.exe
echo Щоб запустити: dist\sokil.exe файл.sokil
