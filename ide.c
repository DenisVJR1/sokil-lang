/*
 * SokilIDE — власне середовище для мови Сокіл.
 * Редактор + підсвітка синтаксису + запуск програми (викликає sokil.exe).
 * 100% власний код на C (Win32 API + RichEdit), без залежностей.
 */
#include <windows.h>
#include <commdlg.h>
#include <richedit.h>
#include <stdio.h>
#include <string.h>

#define ID_OPEN    2001
#define ID_SAVE    2002
#define ID_RUN     2003
#define ID_EDITOR  2101
#define ID_OUTPUT  2102

static HWND hMain, hEdit, hOut;
static HFONT fEd, fBtn;
static int g_busy = 0;                /* захист від ре-входу в підсвітку */

static const char *KEYWORDS[] = {
    "let","if","elif","else","while","for","fn","return","break","continue",
    "and","or","not","true","false","nil"
};
#define KW_N (int)(sizeof KEYWORDS / sizeof KEYWORDS[0])

static const char *BUILTINS[] = {
    "print","input","len","type","num","str","abs","min","max","floor","ceil",
    "round","sqrt","pow","range","push","pop","join","split","exit","random",
    "sleep","contains","now","sort"
};
#define BI_N (int)(sizeof BUILTINS / sizeof BUILTINS[0])

#define C_KW    0x00A45067   /* фіолетовий — ключові слова */
#define C_BI    0x00B96A00   /* помаранчевий — вбудовані */
#define C_STR   0x003B7A00   /* зелений — рядки */
#define C_CMT   0x00908F8B   /* сірий — коментарі */
#define C_NUM   0x000062B3   /* синій — числа */
#define C_TXT   0x001D1B20

#define GETTEXT(ctl) ((int)SendMessageW(ctl, WM_GETTEXT, 0, 0))

/* ── Підсвітка синтаксису ── */
static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}
static void fmt_sel(HWND h, int start, int len, COLORREF col, BOOL bold) {
    CHARFORMAT2W cf = {0};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.dwEffects = bold ? CFE_BOLD : 0;
    cf.crTextColor = col;
    SendMessageW(h, EM_SETSEL, start, start + len);
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}
static void highlight_all(void) {
    if (g_busy) return;
    g_busy = 1;
    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
    int total = GETTEXT(hEdit);
    char *src = (char *)malloc((size_t)total + 1);
    if (src) {
        GETTEXT(hEdit);
        SendMessageW(hEdit, WM_GETTEXT, (WPARAM)(total + 1), (LPARAM)src);
        src[total] = '\0';
        /* скидання до стандартного кольору */
        fmt_sel(hEdit, 0, total, C_TXT, FALSE);
        /* рядки та коментарі */
        int in_str = 0, in_line_cmt = 0, in_block_cmt = 0, str_start = 0;
        for (int i = 0; i < total; i++) {
            char c = src[i], n = (i + 1 < total) ? src[i + 1] : 0;
            if (!in_str && !in_line_cmt && !in_block_cmt && c == '/' && n == '/') in_line_cmt = 1;
            if (!in_str && !in_line_cmt && !in_block_cmt && c == '/' && n == '*') { in_block_cmt = 1; i++; }
            if (!in_str && in_block_cmt && c == '*' && n == '/') { in_block_cmt = 0; i++; }
            if (in_line_cmt && c == '\n') in_line_cmt = 0;
            if (!in_str && !in_line_cmt && !in_block_cmt && c == '"') { in_str = 1; str_start = i; }
            else if (in_str && c == '"' && i > 0 && src[i - 1] != '\\') {
                fmt_sel(hEdit, str_start, i - str_start + 1, C_STR, FALSE);
                in_str = 0;
            }
            if (in_line_cmt)
                fmt_sel(hEdit, i, 1, C_CMT, FALSE);
            if (in_block_cmt)
                fmt_sel(hEdit, i, 1, C_CMT, FALSE);
        }
        /* слова: ключові, вбудовані, числа */
        for (int i = 0; i < total; i++) {
            if (is_word_char(src[i])) {
                int j = i;
                while (j < total && is_word_char(src[j])) j++;
                /* пропускаємо те, що всередині рядка/коментаря */
                int inside = 0;
                int s2 = 0, c2 = 0, b2 = 0;
                for (int k = 0; k < i; k++) {
                    char cc = src[k], nn = (k + 1 < total) ? src[k + 1] : 0;
                    if (!s2 && cc == '"') s2 = 1; else if (s2 && cc == '"' && k > 0 && src[k-1] != '\\') s2 = 0;
                    if (!s2 && !c2 && cc == '/' && nn == '/') c2 = 1;
                    if (c2 && cc == '\n') c2 = 0;
                    if (!s2 && !c2 && cc == '/' && nn == '*') b2 = 1;
                    if (b2 && cc == '*' && nn == '/') { b2 = 0; }
                }
                if (!s2 && !c2 && !b2) {
                    int wl = j - i;
                    char *w = (char *)malloc((size_t)wl + 1);
                    memcpy(w, src + i, wl); w[wl] = 0;
                    int kw = 0, bi = 0;
                    for (int k = 0; k < KW_N; k++) if (!strcmp(w, KEYWORDS[k])) kw = 1;
                    for (int k = 0; k < BI_N; k++) if (!strcmp(w, BUILTINS[k])) bi = 1;
                    if (kw) fmt_sel(hEdit, i, wl, C_KW, TRUE);
                    else if (bi) fmt_sel(hEdit, i, wl, C_BI, FALSE);
                    else if (src[i] >= '0' && src[i] <= '9') fmt_sel(hEdit, i, wl, C_NUM, FALSE);
                    free(w);
                }
                i = j - 1;
            }
        }
        free(src);
        /* курсор у кінець */
        int len = GETTEXT(hEdit);
        SendMessageW(hEdit, EM_SETSEL, len, len);
    }
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
    g_busy = 0;
}

/* ── Знайти sokil.exe: поряд з IDE або в LOCALAPPDATA\Sokil ── */
static void find_sokil(wchar_t *out, int cap) {
    GetModuleFileNameW(NULL, out, cap);
    wchar_t *sl = wcsrchr(out, L'\\');
    if (sl) {
        wcscpy(sl + 1, L"sokil.exe");
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return;
    }
    DWORD sz = GetEnvironmentVariableW(L"LOCALAPPDATA", out, cap);
    if (sz && sz < (DWORD)cap) {
        wcscat(out, L"\\Sokil\\sokil.exe");
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return;
    }
    out[0] = 0;
}

/* ── Виконати програму: зберегти у temp, запустити sokil, зібрати вивід ── */
static void run_program(HWND hw) {
    (void)hw;
    int len = GETTEXT(hEdit);
    char *src = (char *)malloc((size_t)len + 1);
    SendMessageW(hEdit, WM_GETTEXT, (WPARAM)(len + 1), (LPARAM)src);
    src[len] = 0;
    wchar_t tmp[MAX_PATH], sokil[MAX_PATH], outfile[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(tmp + wcslen(tmp), L"sokil-ide.sokil");
    GetTempPathW(MAX_PATH, outfile);
    wsprintfW(outfile + wcslen(outfile), L"sokil-ide.out.txt");
    DeleteFileW(outfile);
    {
        HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            /* перекодовуємо UTF-16 текст у UTF-8 */
            char *u8 = (char *)malloc((size_t)len * 3 + 3);
            DWORD u8len = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)src, len, u8, (int)(len * 3 + 2), NULL, NULL);
            WriteFile(h, u8, u8len, &w, NULL);
            CloseHandle(h);
            free(u8);
        }
    }
    free(src);
    find_sokil(sokil, MAX_PATH);
    if (!sokil[0]) {
        SetWindowTextW(hOut, L"Не знайдено sokil.exe. Встанови Сокіл через інсталятор.");
        return;
    }
    /* через cmd.exe — він парсить перенаправлення > */
    wchar_t desk[2048], comspec[MAX_PATH];
    DWORD cs = GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH);
    if (cs == 0 || cs >= MAX_PATH) wcscpy(comspec, L"C:\\Windows\\System32\\cmd.exe");
    /* простіший формат без зайвих лапок */
    wsprintfW(desk, L"\"%s\" /c \"%s\" \"%s\" > \"%s\" 2>&1", comspec, sokil, tmp, outfile);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi = {0};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessW(NULL, desk, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        SetWindowTextW(hOut, L"Не вдалося запустити sokil.exe.");
        return;
    }
    FILE *fo = _wfopen(outfile, L"rb");
    if (fo) {
        fseek(fo, 0, SEEK_END);
        long sz = ftell(fo);
        fseek(fo, 0, SEEK_SET);
        char *buf = (char *)malloc((size_t)sz + 1);
        size_t got = fread(buf, 1, (size_t)sz, fo);
        buf[got] = 0;
        fclose(fo);
        SetWindowTextW(hOut, L"");
        /* UTF-8 → UTF-16 для RichEdit */
        int u16len = MultiByteToWideChar(CP_UTF8, 0, buf, (int)got, NULL, 0);
        wchar_t *w = (wchar_t *)malloc((size_t)(u16len + 1) * 2);
        MultiByteToWideChar(CP_UTF8, 0, buf, (int)got, w, u16len);
        w[u16len] = 0;
        SetWindowTextW(hOut, w);
        free(w);
        free(buf);
        DeleteFileW(outfile);
    }
}

/* ── Зберегти: писати UTF-8 без BOM ── */
static void save_file(void) {
    int len = GETTEXT(hEdit);
    char *src = (char *)malloc((size_t)len + 1);
    SendMessageW(hEdit, WM_GETTEXT, (WPARAM)(len + 1), (LPARAM)src);
    src[len] = 0;
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = L"Сокіл файли (*.sokil)\0*.sokil\0\0";
    wchar_t path[MAX_PATH] = L"program.sokil";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        FILE *f = _wfopen(path, L"wb");
        if (f) {
            /* конвертуємо UTF-16 текст у UTF-8 */
            char *u8 = (char *)malloc((size_t)len * 3 + 3);
            DWORD u8len = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)src, len, u8, (int)(len * 3 + 2), NULL, NULL);
            if (u8len == 0) u8len = (DWORD)strlen(src);
            fwrite(u8, 1, u8len, f);
            fclose(f);
            SetWindowTextW(hMain, path);
        }
    }
    free(src);
}
static void open_file(void) {
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = L"Сокіл файли (*.sokil)\0*.sokil\0Усі файли\0*.*\0\0";
    wchar_t path[MAX_PATH] = L"";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    if (GetOpenFileNameW(&ofn)) {
        FILE *f = _wfopen(path, L"rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *u8 = (char *)malloc((size_t)sz + 2);
            size_t got = fread(u8, 1, (size_t)sz, f);
            u8[got] = 0;
            fclose(f);
            /* UTF-8 → UTF-16 (ігноруючи можливий BOM) */
            int u16len = MultiByteToWideChar(CP_UTF8, 0, u8, (int)got, NULL, 0);
            wchar_t *w = (wchar_t *)malloc((size_t)(u16len + 1) * 2);
            MultiByteToWideChar(CP_UTF8, 0, u8, (int)got, w, u16len);
            w[u16len] = 0;
            SetWindowTextW(hEdit, w);
            free(w); free(u8);
            highlight_all();
            SetWindowTextW(hMain, path);
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        hMain = hw;
        fEd  = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");
        fBtn = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        CreateWindowW(L"BUTTON", L"Відкрити", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 8, 8, 80, 30, hw, (HMENU)ID_OPEN, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Зберегти", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 92, 8, 80, 30, hw, (HMENU)ID_SAVE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Виконати ▶", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 176, 8, 100, 30, hw, (HMENU)ID_RUN, NULL, NULL);
        LoadLibraryW(L"msftedit.dll");
        hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit50W", L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL, 8, 46, 784, 420, hw, (HMENU)ID_EDITOR, NULL, NULL);
        SendMessageW(hEdit, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)fEd, TRUE);
        SetWindowTextW(hEdit,
            L"// Програма на Сокіл\n"
            L"let i = 0\n"
            L"while i < 5 {\n"
            L"    i = i + 1\n"
            L"    print('рядок ' + str(i))\n"
            L"}\n"
            L"fn add(a, b) { return a + b }\n"
            L"print(add(2, 3))\n"
            L"print('hi' * 3)   // повтор рядка\n"
            L"let sokil = input('як тебе звати? ')\n"
            L"print('привіт, ' + sokil)\n");
        hOut = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit50W", L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY, 8, 472, 784, 130, hw, (HMENU)ID_OUTPUT, NULL, NULL);
        SendMessageW(hOut, WM_SETFONT, (WPARAM)fEd, TRUE);
        SetWindowTextW(hOut, L"Вивід програми з'явиться тут…");
        highlight_all();
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hw, &rc);
        int w = rc.right, h = rc.bottom;
        MoveWindow(hEdit, 8, 46, w - 16, h - 46 - 150, TRUE);
        MoveWindow(hOut, 8, h - 140, w - 16, 132, TRUE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OPEN:  open_file(); break;
        case ID_SAVE:  save_file(); break;
        case ID_RUN: {
            int len = GETTEXT(hEdit);
            (void)len;
            run_program(hw);
            break;
        }
        default:
            if (LOWORD(wp) == ID_EDITOR && HIWORD(wp) == EN_CHANGE)
                highlight_all();
            break;
        }
        return 0;
    case WM_DESTROY:
        DeleteObject(fEd); DeleteObject(fBtn);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd; (void)show;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SokilIDE";
    RegisterClassW(&wc);
    HWND hw = CreateWindowExW(0, L"SokilIDE", L"SokilIDE — середовище для Сокіл",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 620, NULL, NULL, hInst, NULL);
    if (!hw) return 1;
    ShowWindow(hw, SW_SHOW);
    UpdateWindow(hw);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}