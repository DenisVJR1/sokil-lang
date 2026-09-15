#!/bin/bash
# Збірка бінарника для macOS
echo "🦅 Збірка Сокіл для macOS..."
pip install pyinstaller 2>/dev/null || pip3 install pyinstaller 2>/dev/null
pyinstaller --onefile --name sokil sokil.py
echo "✅ Готово: dist/sokil"
echo "Щоб запустити: ./dist/sokil файл.sokil"
