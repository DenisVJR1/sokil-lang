# Сокіл 🦅 — Sokil Programming Language

Проста, але справжня мова програмування: нуль залежностей, один файл на чистому C (C99), нативні бінарники, власний інсталятор та власна IDE — все написано з нуля.

**Zero dependencies. 100% власний код.**

---

## 🇺🇦 Українською

### Можливості

- ✅ Лексер → парсер → інтерпретатор (tree-walking) на чистому C — **без Python, без C-компілятора**
- ✅ `sokil --compile файл.sokil` → **готовий .exe** (своя компіляція, C не потрібен)
- ✅ `SokilIDE.exe` — **власна IDE** з підсвіткою синтаксису та запуском (встановлюється автоматично)
- ✅ Windows / Linux / macOS
- ✅ `sokil --update` — самооновлення з GitHub
- ✅ Подвійний клік по `.sokil` — запуск (асоціація ставиться інсталятором)
- ✅ **Самовідновлення PATH** — `sokil` сам повертає свій запис у PATH, якщо він зник
- ✅ Власний інсталятор-майстер (wizard) з анімованим фоном з коду

### Встановлення (Windows)

**Власний інсталятор** (написаний на C, вбудовує і `sokil.exe`, і `SokilIDE.exe`):

```
Sokil-Setup.exe            # майстер: Вітання → Шлях → Готово
Sokil-Setup.exe --install  # тиха установка
Sokil-Setup.exe --uninstall
```

- Встановлює **обидва** файли у `%LOCALAPPDATA%\Sokil`: мову + IDE автоматично
- Галочка **«Патчити PATH»** — запуск мови з будь-якого терміналу
- **«Версія»** — вбудована (офлайн), latest або конкретний тег з GitHub
- Ставить асоціацію `.sokil` — подвійний клік по файлу запускає програму через `cmd /k`

**Linux / macOS**:
```bash
sudo ./install.sh          # збирає і ставить у /usr/local/bin
```

### Запуск

```bash
./sokil                  # REPL
./sokil файл.sokil       # виконати програму
./sokil -e "print(1)"    # виконати рядок
./sokil --compile app.sokil   # app.exe без C-компілятора!
./sokil --update         # самооновлення з GitHub
./sokil --version        # версія
```

### Приклад коду

```
// класична програма
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

let name = input('Як тебе звати? ')
print('Привіт, ' + name + '!')
print('Фібоначчі(10) = ' + fib(10))

// сучасні фічі
let arr = [9, 2, 7, 1]
sort(arr)                 // сортування на місці
print(arr)                // [1, 2, 7, 9]
print('Сокіл' * 2)        // СокілСокіл
print(now())              // Unix-час у секундах
print('тест ' + 42)       // рядок + число = конкатенація
```

### Синтаксис

- Змінні: `let x = 42`, `let s = "рядок"` або `'рядок'`
- Рядки: подвійні `"..."` **та одинарні** `'...'` лапки
- Умови: `if / elif / else`
- Цикли: `while умова { }`, `for i = 0; i < n; i = i + 1 { }`, `break`, `continue`
- Функції: `fn name(арг) { return ... }` + замикання
- Масиви: `[1,2,3]`, `push`, `pop`, `join`, `split`, індекси
- Вбудовані: `print input len type str num abs min max floor ceil round sqrt pow range push pop join split exit sleep contains now sort random sin cos tan log exp pi e deg rad map filter reduce reverse shuffle sum slice upper lower trim replace startswith endswith substr is_num is_str is_arr is_bool is_nil is_func read_file write_file append_file exists is_file is_dir list_dir date format_time system pid exec http_get download b64_encode b64_decode hex uuid env`
- Оператори: `+ - * / % == != < > <= >= and or not`
- Тернарний: `age >= 18 ? "дорослий" : "малий"`
- Помилки: `try { ... } catch err { ... }` — `err` — текст помилки (змінна необов'язкова)
- Складені присвоєння: `x += 1`, `x -= 2`, `x *= 3`, `x /= 4`, `x %= 5`
- Обхід масивів: `for x in arr { }` (foreach)
- Об'єкти: `{name: "Іван", age: 25}` — доступ `obj[0]`, `obj[1]`; JSON-сумісні
- Модулі: `import "файл.sokil"` — виконує файл у поточному середовищі
- JSON: `json_parse(str)`, `json_stringify(масив_або_значення)`
- Коментарі: `//`, `#` та `/* */`
- Скорочення: `i++`, `i--`; опційний розділювач `;`

Приклади — у папці `examples/`.

---

## 🇬🇧 English

A simple but real programming language: zero dependencies, one pure-C (C99) file, native binaries, plus its own installer and IDE — everything written from scratch.

**Zero dependencies. 100% our own code.**

### Features

- ✅ Lexer → parser → interpreter (tree-walking) in pure C — **no Python, no C compiler needed**
- ✅ `sokil --compile file.sokil` → **ready .exe** (own compilation, no C involved)
- ✅ `SokilIDE.exe` — **own IDE** with syntax highlighting and run button (installed automatically)
- ✅ Windows / Linux / macOS
- ✅ `sokil --update` — self-update from GitHub
- ✅ Double-click `.sokil` to run (association set by installer)
- ✅ **PATH self-heal** — `sokil` restores its PATH entry automatically if it disappears
- ✅ Own wizard installer with animated code background

### Install (Windows)

**Our own installer** (written in C, embeds both `sokil.exe` and `SokilIDE.exe`):

```
Sokil-Setup.exe            # wizard: Welcome → Path → Done
Sokil-Setup.exe --install  # silent install
Sokil-Setup.exe --uninstall
```

- Installs **both** files to `%LOCALAPPDATA%\Sokil`: the language + the IDE automatically
- **"Patch PATH"** checkbox — run the language from any terminal
- **"Version"** dropdown — embedded (offline), latest, or a specific GitHub tag
- Sets the `.sokil` association — double-clicking a file runs the program via `cmd /k`

**Linux / macOS**:
```bash
sudo ./install.sh          # builds and installs to /usr/local/bin
```

### Usage

```bash
./sokil                  # REPL
./sokil file.sokil       # run a program
./sokil -e "print(1)"    # run one line
./sokil --compile app.sokil   # app.exe WITHOUT a C compiler!
./sokil --update         # self-update from GitHub
./sokil --version        # version
```

### Code example

```
// classic program
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

let name = input('What is your name? ')
print('Hello, ' + name + '!')
print('fib(10) = ' + fib(10))

// modern features
let arr = [9, 2, 7, 1]
sort(arr)                 // in-place sort
print(arr)                // [1, 2, 7, 9]
print('Sokil' * 2)        // SokilSokil
print(now())              // Unix time in seconds
print('test ' + 42)       // string + number = concatenation
```

### Syntax

- Variables: `let x = 42`, `let s = "string"` or `'string'`; **assignment auto-declares**: `x = 5` works without `let`
- Strings: double `"..."` **and single** `'...'` quotes
- Conditions: `if / elif / else`
- Loops: `while cond { }`, `for i = 0; i < n; i = i + 1 { }`, `break`, `continue`
- Functions: `fn name(args) { return ... }` + closures
- Arrays: `[1,2,3]`, `push`, `pop`, `join`, `split`, indexing
- Comments: `//`, `#`, `/* */`
- Run: `sokil file` — `.sokil` extension is optional
- Built-ins: `print input len type str num abs min max floor ceil round sqrt pow range push pop join split exit sleep contains now sort random sin cos tan log exp pi e deg rad map filter reduce reverse shuffle sum slice upper lower trim replace startswith endswith substr is_num is_str is_arr is_bool is_nil is_func read_file write_file append_file exists is_file is_dir list_dir date format_time system pid exec http_get download b64_encode b64_decode hex uuid env`
- Operators: `+ - * / % == != < > <= >= and or not`
- Ternary: `age >= 18 ? "дорослий" : "малий"`
- Errors: `try { ... } catch err { ... }` — `err` holds the message (variable optional)
- Compound assignment: `x += 1`, `x -= 2`, `x *= 3`, `x /= 4`, `x %= 5`
- Iteration: `for x in arr { }` (foreach)
- Objects: `{name: "Іван", age: 25}` — access `obj[0]`, `obj[1]`; JSON-compatible
- Modules: `import "file.sokil"` — runs the file in the current environment
- JSON: `json_parse(str)`, `json_stringify(array_or_value)`
- Comments: `//`, `#`, `/* */`
- Shorthand: `i++`, `i--`; optional `;` separator

Examples in `examples/`.

---

## Build

```bash
# Linux / macOS
cc -O2 -std=c99 -lm -o sokil sokil.c

# Windows (MinGW)
gcc -O2 -std=c99 -lm -o sokil.exe sokil.c -lurlmon
```

Prebuilt binaries: [Releases](https://github.com/DenisVJR1/sokil-lang/releases).

## Architecture

- `sokil.c` — lexer → parser → interpreter (tree-walking), arena allocator
- `installer.c` + `sokil_exe.h` + `sokil_ide.h` — own Windows wizard installer with embedded language + IDE
- `ide.c` — own IDE (RichEdit + syntax highlighting, built-in run button)

## License

MIT