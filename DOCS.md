# Сокіл (Sokil) — Повна документація

## Зміст
1. [Встановлення](#встановлення)
2. [Запуск](#запуск)
3. [Синтаксис мови](#синтаксис-мови)
4. [Вбудовані функції](#вбудовані-функції)
5. [Приклади програм](#приклади-програм)
6. [Архітектура](#архітектура)

---

## Встановлення

### Windows — GUI інсталятор `Sokil-Setup.exe`
- **Інтерактивний режим:** просто запустіть `Sokil-Setup.exe` — відкриється вікно з кнопками "Встановити" / "Видалити"
- **Командний рядок:**
  ```
  Sokil-Setup.exe --install    # тиха установка
  Sokil-Setup.exe --uninstall  # повне видалення
  ```
- Що робить інсталятор:
  - Копіює `sokil.exe` у `%LOCALAPPDATA%\Sokil\`
  - Додає цю папку до **PATH** користувача (HKCU\Environment)
  - Реєструє асоціацію файлів **`.sokil`** — подвійний клік по файлу запускає його
  - Надсилає `WM_SETTINGCHANGE` — PATH оновлюється миттєво в нових вікнах терміналу

### Linux / macOS
```bash
git clone https://github.com/DenisVJR1/sokil-lang
cd sokil-lang
sudo ./install.sh
```

Або збірка вручну:
```bash
cc -O2 -std=c99 -lm -o sokil sokil.c
sudo install -m 755 sokil /usr/local/bin/sokil
```

---

## Запуск

```bash
# REPL (інтерактивний режим)
sokil

# Виконати файл
sokil program.sokil

# Виконати рядок коду
sokil -e "print(2 + 2)"

# Передати аргументи програмі (доступні як args)
sokil program.sokil arg1 arg2 arg3
# у програмі: print(args)  ->  [arg1, arg2, arg3]
```

---

## Синтаксис мови

### Коментарі
```sokil
// це рядковий коментар

/* це
   блоковий
   коментар */
```

### Змінні
```sokil
let x = 42              // число
let name = "Сокіл"      // рядок
let pi = 3.14           // число з плаваючою комою
let flag = true         // логічне значення
let nothing = nil       // відсутність значення
```

- **Динамічна типізація** — тип визначається значенням
- `let` оголошує нову змінну (переприсвоєння: `x = 10`)
- Рядки в подвійних лапках, підтримують Unicode

### Типи даних
| Тип | Приклад | Опис |
|-----|---------|------|
| `число` | `42`, `3.14`, `-7` | double (64-bit) |
| `рядок` | `"текст"`, `"🦅"` | UTF-8 рядок |
| `логічне` | `true`, `false` | булеве значення |
| `масив` | `[1, 2, "a"]` | динамічний масив |
| `функція` | `fn() {}` | користувацька або вбудована |
| `nil` | `nil` | відсутність значення |

### Оператори
```sokil
// Арифметичні
+  -  *  /  %

// Порівняння (повертають true/false)
==  !=  <  >  <=  >=

// Логічні
and   or   not   !   // not та ! — синоніми

// Присвоєння
=  // let x = 1; x = 2

// Індексація (рядки та масиви)
arr[0]     // перший елемент
str[0]     // перший символ
```

### Умови
```sokil
if x > 10 {
    print("більше")
} elif x == 10 {
    print("рівно")
} else {
    print("менше")
}
```

### Цикли

**while:**
```sokil
let i = 0
while i < 10 {
    print(i)
    i = i + 1
}
```

**for (C-style):**
```sokil
for i = 0; i < 10; i = i + 1 {
    if i == 5 { continue }  // пропустити ітерацію
    if i == 8 { break }     // вийти з циклу
    print(i)
}
```

**break / continue** працюють у `while` та `for`. `continue` у `for` переходить до кроку (`i = i + 1`).

### Функції
```sokil
fn fact(n) {
    if n <= 1 { return 1 }
    return n * fact(n - 1)
}

print(fact(5))  // 120
```

- `fn name(params) { body }` — оголошення
- `return value` — повернення значення (опціонально, останній вираз повертається автоматично)
- **Замикання** — функція пам'ятає змінні зовнішнього scope:
```sokil
fn counter() {
    let i = 0
    fn inc() {
        i = i + 1
        return i
    }
    return inc
}

let c = counter()
print(c())  // 1
print(c())  // 2
```

### Масиви
```sokil
let a = [1, 2, 3]
print(a[0])      // 1
print(len(a))    // 3
a[1] = 99        // [1, 99, 3]
```

**Функціональні методи (повертають НОВИЙ масив):**
```sokil
let b = push(a, 4)    // [1, 99, 3, 4]
let c = pop(b)        // [1, 99, 3]
let r = range(5)      // [0, 1, 2, 3, 4]
let r2 = range(2, 6)  // [2, 3, 4, 5]
```

### Рядки
```sokil
let s = "Привіт"
print(s[0])           // П
print(len(s))         // 6
print("a" + "b")      // ab (конкатенація)

let words = split("a,b,c", ",")  // ["a", "b", "c"]
print(join(words, "-"))          // "a-b-c"
```

### Аргументи командного рядка
```sokil
// запуск: sokil prog.sokil one two three
print(args)      // ["one", "two", "three"]
print(args[0])   // "one"
```

### Випадкові числа
```sokil
print(random())       // double 0.0 .. 1.0
print(random(10))     // ціле 0 .. 9
print(random(5, 15))  // ціле 5 .. 14
```

---

## Вбудовані функції

### Введення / Вивід
| Функція | Опис |
|---------|------|
| `print(...)` | Вивід значень через пробіл + newline |
| `input(prompt?)` | Зчитування рядка з клавіатури |

### Перетворення типів
| Функція | Опис |
|---------|------|
| `str(x)` | Будь-що → рядок |
| `num(x)` | Рядок/число → число |
| `type(x)` | Повертає назву типу: `"число"`, `"рядок"`, `"логічне"`, `"масив"`, `"функція"`, `"nil"` |

### Математика
| Функція | Опис |
|---------|------|
| `abs(x)` | Модуль |
| `min(a,b)` | Мінімум |
| `max(a,b)` | Максимум |
| `floor(x)` | Округлення вниз |
| `ceil(x)` | Округлення вгору |
| `round(x)` | Округлення до найближчого |
| `sqrt(x)` | Квадратний корінь (x ≥ 0) |
| `pow(a,b)` | a^b |

### Масиви
| Функція | Опис |
|---------|------|
| `len(arr)` | Довжина масиву |
| `range(n)` | `[0, 1, ..., n-1]` |
| `range(a,b)` | `[a, a+1, ..., b-1]` |
| `push(arr, v)` | Новий масив з доданим `v` в кінець |
| `pop(arr)` | Новий масив без останнього елемента |

### Рядки
| Функція | Опис |
|---------|------|
| `len(str)` | Довжина рядка |
| `split(str, sep)` | Розбити рядок по роздільнику → масив |
| `join(arr, sep)` | З'єднати масив рядків через `sep` |

### Інші
| Функція | Опис |
|---------|------|
| `exit(code?)` | Завершити програму (код 0 за замовчуванням) |
| `random()` | Випадкове число 0.0..1.0 |
| `random(n)` | Випадкове ціле 0..n-1 |
| `random(a,b)` | Випадкове ціле a..b-1 |

---

## Приклади програм

Всі приклади є в папці `examples/`:

### hello.sokil
```sokil
print("Привіт, Сокіл! 🦅")
```

### factorial.sokil
```sokil
fn fact(n) {
    if n <= 1 { return 1 }
    return n * fact(n - 1)
}

for i = 1; i <= 10; i = i + 1 {
    print(i, "! =", fact(i))
}
```

### fibonacci.sokil
```sokil
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

for i = 0; i < 15; i = i + 1 {
    print("fib(", i, ") =", fib(i))
}
```

### arrays.sokil
```sokil
let a = [10, 20, 30]
print("Початковий:", a)
print("Довжина:", len(a))

a[0] = 99
print("Після зміни:", a)

let b = push(a, 40)
print("push:", b)

let c = pop(b)
print("pop:", c)

let r = range(5)
print("range(5):", r)
let r2 = range(2, 8)
print("range(2,8):", r2)
```

### sorting.sokil (бульбашкове сортування)
```sokil
fn bubble(arr) {
    let n = len(arr)
    for i = 0; i < n - 1; i = i + 1 {
        for j = 0; j < n - i - 1; j = j + 1 {
            if arr[j] > arr[j + 1] {
                let tmp = arr[j]
                arr[j] = arr[j + 1]
                arr[j + 1] = tmp
            }
        }
    }
    return arr
}

let data = [64, 34, 25, 12, 22, 11, 90]
print("До:", data)
print("Після:", bubble(data))
```

### recursion.sokil (замикання — лічильник)
```sokil
fn make_counter() {
    let count = 0
    fn inc() {
        count = count + 1
        return count
    }
    return inc
}

let c1 = make_counter()
let c2 = make_counter()

print(c1())  // 1
print(c1())  // 2
print(c2())  // 1 (незалежний лічильник)
print(c1())  // 3
```

### guessing.sokil (гра "Вгадай число")
```sokil
let secret = random(1, 101)
let attempts = 0

print("Вгадай число від 1 до 100!")

while true {
    let guess = num(input("Твій варіант: "))
    attempts = attempts + 1

    if guess == secret {
        print("Вгадав за", attempts, "спроб!")
        break
    } elif guess < secret {
        print("Більше!")
    } else {
        print("Менше!")
    }
}
```

### Додаткові приклади

#### math_demo.sokil
```sokil
print("Математика:")
print("abs(-7)     =", abs(-7))
print("min(3,9)    =", min(3, 9))
print("max(3,9)    =", max(3, 9))
print("floor(3.7)  =", floor(3.7))
print("ceil(3.2)   =", ceil(3.2))
print("round(3.5)  =", round(3.5))
print("sqrt(16)    =", sqrt(16))
print("pow(2,10)   =", pow(2, 10))
```

#### string_demo.sokil
```sokil
let text = "Сокіл літає високо"
print("Рядок:", text)
print("Довжина:", len(text))
print("Символ 0:", text[0])
print("Символ 5:", text[5])

let words = split(text, " ")
print("Слова:", words)
print("З'єднано:", join(words, "|"))
```

#### args_demo.sokil
```sokil
// запуск: sokil args_demo.sokil one two three
print("Аргументи:", args)
print("Кількість:", len(args))

for i = 0; i < len(args); i = i + 1 {
    print("arg[", i, "] =", args[i])
}
```

#### random_demo.sokil
```sokil
print("random():", random())
print("random(6):", random(6))        // кубик 0-5
print("random(1,7):", random(1, 7))   // кубик 1-6

// симуляція 10 кидків кубика
let rolls = []
for i = 0; i < 10; i = i + 1 {
    rolls = push(rolls, random(1, 7))
}
print("10 кидків:", rolls)
```

---

## Архітектура

```
sokil.c (єдиний файл, ~1500 рядків)
├── Arena alokator      // блоковий, без фрагментації, без free
├── Лексер              // токенизація з підтримкою Unicode
├── Парсер (recursive descent, precedence climbing)
│   ├── вирази
│   ├── інструкції (if, while, for, fn, return, break, continue)
│   └── блоки
├── AST вузли
├── Інтерпретатор (tree-walking)
│   ├── оточення (env) з ланкувням для замикань
│   ├── longjmp для return/break/continue
│   └── вбудовані функції (native)
└── main + REPL

installer.c + sokil_exe.h
├── Win32 GUI (без залежностей, -mwindows)
├── Вбудований sokil.exe як байтовий масив
├── Реєстрація PATH (HKCU\Environment)
├── Асоціація .sokil (HKCU\Software\Classes)
└── WM_SETTINGCHANGE broadcast
```

**Властивості:**
- **Zero dependencies** — лише стандартна бібліотека C (libc, libm, Win32 API)
- **Статичне лінкування** — працює на будь-якому Windows/Linux/macOS без DLL
- **Арена-алокатор** — швидкий, без утечок, підходить для рекурсії
- **Tree-walking інтерпретатор** — простий, без байт-коду/JIT

---

## Ліцензія

MIT License — вільне використання, модифікація, розповсюдження.

---

*Створено з ❤️ українською мовою програмування **Сокіл** 🦅*