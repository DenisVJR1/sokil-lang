# Сокіл 🦅 (Sokil)

Дуже проста мова програмування. Одна реалізація на чистих C — нуль залежностей,
нативний бінарник для Windows, Linux та macOS.

## Збірка

```bash
# Linux / macOS
cc -O2 -std=c99 -o sokil sokil.c

# Windows (MinGW)
gcc -O2 -std=c99 -o sokil.exe sokil.c
```

Або готові збірки: [Releases](https://github.com/DenisVJR1/sokil-lang/releases).

## Встановлення

```bash
# Linux / macOS — встановить у /usr/local/bin
sudo ./install.sh

# Windows — збере у %LOCALAPPDATA%\Sokil і додасть у PATH
install.bat
```

Або справжній Windows-інсталятор з [Releases](https://github.com/DenisVJR1/sokil-lang/releases)
(зібраний із `setup.iss` через Inno Setup).

## Запуск

```bash
./sokil                 # REPL
./sokil файл.sokil      # виконати програму
```

## Синтаксис мови

### Змінні
```
let x = 42
let name = "Сокіл"
let pi = 3.14
let flag = true
let nothing = nil
```

### Вивід та введення
```
print("Привіт!")
print(x)
let name = input("Як тебе звати? ")
print("Привіт, " + name + "!")
```

### Математика та порівняння
```
let result = (2 + 3) * 4
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

### Функції та рекурсія
```
fn fib(n) {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}
print(fib(10))
```

### Масиви
```
let arr = [1, 2, 3, 4, 5]
print(arr[0])      // 1
print(len(arr))    // 5
arr[0] = 99        // тепер [99, 2, 3, 4, 5]
```

### Вбудовані функції

| Функція    | Опис                           |
|------------|--------------------------------|
| `print()`  | Вивід на екран                 |
| `input()`  | Зчитування з клавіатури        |
| `len()`    | Довжина рядка/масиву           |
| `type()`   | Тип змінної                    |
| `str()`    | Перетворення на рядок          |
| `num()`    | Перетворення на число          |

Коментарі: `// рядок`. Логічні операції: `and`, `or`, `not` (або `!`).
Оператори: `+ - * / % == != < > <= >=`.

## Приклади

Папка `examples/`:
- `hello.sokil` — привіт, світ
- `fibonacci.sokil` — числа Фібоначчі
- `factorial.sokil` — факторіал
- `arrays.sokil` — робота з масивами
- `sorting.sokil` — сортування
- `recursion.sokil` — рекурсія та замикання
- `guessing.sokil` — гра «Вгадай число»

## Ліцензія

MIT