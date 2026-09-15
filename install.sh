#!/bin/sh
# Встановлення Сокіл для Linux / macOS
# Використання: sudo ./install.sh
set -e

echo "🦅 Збираємо Сокіл..."
cc -O2 -std=c99 -o sokil sokil.c

echo "📦 Встановлюємо у /usr/local/bin..."
install -m 755 sokil /usr/local/bin/sokil

rm -f sokil
echo "✅ Встановлено! Запуск: sokil (REPL) або sokil файл.sokil"
echo "   Перевірка версії розпізнавання: sokil"