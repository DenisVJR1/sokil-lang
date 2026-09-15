# Сокіл 🦅 (Sokil)

Дуже проста мова програмування. Одна реалізація на чистих C — нуль залежностей,
нативний бінарник для Windows, Linux та macOS.

## Збірка

```bash
# Linux / macOS
cc -O2 -std=c99 -lm -o sokil sokil.c

# Windows (MinGW)
gcc -O2 -std=c99 -lm -o sokil.exe sokil.c
```

Або готові збірки: [Releases](https://github.com/DenisVJR1/sokil-lang/releases).

## Встановлення

**Windows** — власний інсталятор (написаний на C, `installer.c`, вбудовує `sokil.exe`):
```
Sokil-Setup.exe            # інтерактивне меню
Sokil-Setup.exe --install  # тиха установка
Sokil-Setup.exe --uninstall
```
Встановлює мову у `%LOCALAPPDATA%\Sokil` і **прописує PATH** у реєстрі (як Python installer).

**Linux / macOS**:
```bash
sudo ./install.sh          # збирає і ставить у /usr/local/bin
```

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

### Цикли: while, for, break, continue
```
let i = 0
while i < 10 {
    print(i)
    i = i + 1
}

for i = 0; i < 10; i = i + 1 {
    if i == 5 {
        continue
    }
    if i == 8 {
        break
    }
    print(i)
}
```

### Умови
```
if x > 10 {
    print("більше")
} elif x == 10 {
    print("рівно")
} else {
    print("менше")
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
print(arr[0])       // 1
print(len(arr))     // 5
arr[0] = 99         // [99, 2, 3, 4, 5]
arr = push(arr, 6)  // [99, 2, 3, 4, 5, 6]
arr = pop(arr)      // [99, 2, 3, 4, 5]
print(range(5))     // [0, 1, 2, 3, 4]
print(range(2, 6))  // [2, 3, 4, 5]
```

### Рядки
```
let words = split("яблуко,груша,слива", ",")
print(words)                        // [яблуко, груша, слива]
print(join(words, " - "))           // яблуко - груша - слива
print("рядок"[0])                   // р
print(len("Сокіл"))                 // 5
```

## Вбудовані функції

| Функція          | Опис                                  |
|------------------|---------------------------------------|
| `print(...)`     | Вивід на екран                        |
| `input(промпт)`  | Зчитування з клавіатури               |
| `len(x)`         | Довжина рядка/масиву                  |
| `type(x)`        | Тип змінної                           |
| `str(x)`         | Перетворення на рядок                 |
| `num(x)`         | Перетворення на число                 |
| `abs(x)`         | Модуль числа                          |
| `min(a,b)`       | Менше з двох                          |
| `max(a,b)`       | Більше з двох                         |
| `floor(x)`       | Округлення вниз                       |
| `ceil(x)`        | Округлення вгору                      |
| `round(x)`       | Округлення                            |
| `sqrt(x)`        | Квадратний корінь                     |
| `pow(a,b)`       | a у степені b                         |
| `range(a[,b])`   | Масив чисел [a..b)                    |
| `push(arr,v)`    | Новий масив з доданим значенням       |
| `pop(arr)`       | Новий масив без останнього елемента   |
| `join(arr,sep)`  | З'єднати масив у рядок                |
| `split(str,sep)` | Розбити рядок на масив                |
| `exit([код])`    | Завершити програму                    |
| `sleep(мс)`      | Пауза в мілісекундах                  |
| `contains(р,під)` | Чи містить рядок підрядок            |

Скорочення: `i++` = `i = i + 1`, `i--` = `i = i - 1`.

Коментарі: `// рядок` та `/* блок */`. Логічні операції: `and`, `or`, `not` (або `!`).
Оператори: `+ - * / % == != < > <= >=`. Опційний розділювач: `;`.

## Приклади

Папка `examples/`:
- `hello.sokil` — привіт, світ
- `fibonacci.sokil` — числа Фібоначчі
- `factorial.sokil` — факторіал
- `arrays.sokil` — робота з масивами
- `sorting.sokil` — сортування
- `recursion.sokil` — рекурсія та замикання
- `guessing.sokil` — гра «Вгадай число»

## Архітектура

- `sokil.c` — лексер → парсер → інтерпретатор (tree-walking), арена-алокатор
- `installer.c` + `sokil_exe.h` — власний інсталятор Windows з вбудованим бінарником
- `setup.iss` — альтернатива: Inno Setup інсталятор

## Ліцензія

MIT