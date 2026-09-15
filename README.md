# Сокіл 🦅 (Sokil)

Дуже проста мова програмування. Нуль залежностей, лише Python stdlib.

## Швидкий старт

```bash
# Python (потрібен Python 3.8+)
python sokil.py

# Або з файлом
python sokil.py examples/hello.sokil
```

## Збірка бінарника

### Linux
```bash
chmod +x build-linux.sh
./build-linux.sh
```

### macOS
```bash
chmod +x build-mac.sh
./build-mac.sh
```

### Windows
```cmd
build-windows.bat
```

### Автоматично (GitHub Actions)
Додайте тег і запуште:
```bash
git tag v1.0
git push origin v1.0
```
Скрипт автоматично збере `.exe` для Windows, бінарники для Linux та macOS.

## Синтаксис мови

### Змінні
```
let x = 42
let name = "Сокіл"
let pi = 3.14
let flag = true
let nothing = nil
```

### Вивід
```
print("Привіт!")
print(x)
print("Значення: " + str(x))
```

### Математика
```
let result = (2 + 3) * 4
let mod = 10 % 3
```

### Порівняння
```
if x > 10 {
    print("більше")
} elif x == 10 {
    print("рівно")
} else {
    print("менше")
}
```

### Цикли
```
let i = 0
while i < 10 {
    print(i)
    i = i + 1
}
```

### Функції
```
fn add(a, b) {
    return a + b
}

print(add(3, 4))  // 7
```

### Рекурсія
```
fn fib(n) {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}
```

### Масиви
```
let arr = [1, 2, 3, 4, 5]
print(arr[0])      // 1
print(len(arr))    // 5

arr[0] = 99        // тепер [99, 2, 3, 4, 5]
```

### Введення з клавіатури
```
let name = input("Як тебе звати? ")
print("Привіт, " + name + "!")
```

### Вбудовані функції

| Функція    | Опис                            |
|------------|---------------------------------|
| `print()`  | Вивід на екран                  |
| `input()`  | Зчитування з клавіатури         |
| `len()`    | Довжина рядка/масиву            |
| `type()`   | Тип змінної                     |
| `str()`    | Перетворення на рядок           |
| `num()`    | Перетворення на число           |

## Приклади

Дивіться папку `examples/`:
- `hello.sokil` — привіт, світ
- `fibonacci.sokil` — числа Фібоначчі
- `factorial.sokil` — факторіал
- `arrays.sokil` — робота з масивами
- `sorting.sokil` — сортування
- `recursion.sokil` — рекурсія та замикання

## Вимоги

- Python 3.8+
- Ніяких зовнішніх залежностей

## Ліцензія

MIT