#!/bin/bash
# Збірка Сокіл для macOS
mkdir -p dist
cc -O2 -std=c99 -lm -o dist/sokil sokil.c
echo "Готово: dist/sokil"
echo "Запуск: ./dist/sokil файл.sokil"