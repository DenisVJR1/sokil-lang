#!/bin/bash
# Збірка бінарника для Linux
echo "🦅 Збірка Сокіл для Linux..."
pip install pyinstaller 2>/dev/null || pip3 install pyinstaller 2>/dev/null
pyinstaller --onefile --name sokil sokil.py
echo "✅ Готово: dist/sokil"
echo "Щоб запустити: ./dist/sokil файл.sokil"
