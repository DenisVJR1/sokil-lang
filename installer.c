/*
 * Сокіл — інсталятор у стилі Inno Setup, дизайн Material 3.
 * 100% власний код на C (Win32 API), без залежностей.
 *
 * Можливості:
 *   - Встановлення / Оновлення (patch) / Видалення
 *   - PATH у реєстрі (HKCU) + WM_SETTINGCHANGE
 *   - Асоціація файлів .sokil
 *   - Вбудований sokil.exe (байтовий масив з sokil_exe.h)
 *
 * Збірка: gcc -O2 -std=c99 -static -mwindows -o Sokil-Setup.exe installer.c
 *         -luser32 -ladvapi32 -lshell32 -ldwmapi
 *
 * CLI: Sokil-Setup.exe --install | --uninstall | --version
 */

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <string.h>
#include "sokil_exe.h"

#define BTN_INSTALL   1001
#define BTN_UPDATE    1002
#define BTN_UNINSTALL 1003

/* ── Палітра Material 3 (seed #6750A4) ── */
#define M3_SURFACE      RGB(0xFE,0xF7,0xFF)  /* поверхня */
#define M3_CARD         RGB(0xF3,0xED,0xF7)  /* контейнер */
#define M3_PRIMARY      RGB(0x67,0x50,0xA4)  /* акцент */
#define M3_PRIMARY_DARK RGB(0x59,0x45,0x8E)
#define M3_PRIMARY_LT   RGB(0x76,0x58,0xB8)
#define M3_PRIMARY_TXT  RGB(0x21,0x00,0x5D)
#define M3_TONAL        RGB(0xEA,0xDD,0xFF)  /* вторинна */
#define M3_TONAL_TXT    RGB(0x21,0x00,0x5D)
#define M3_ON_SURFACE   RGB(0x1D,0x1B,0x20)
#define M3_MUTED        RGB(0x49,0x45,0x4F)
#define M3_OUTLINE      RGB(0x79,0x74,0x7E)
#define M3_OK           RGB(0x38,0x6A,0x20)
#define M3_ERR          RGB(0xB3,0x26,0x1E)

static wchar_t APP_DIR[MAX_PATH];
static HWND hMain;
static HWND hChkPath;
static HBRUSH hbrCard;

/* ── PATH ── */
static void notify(void) {
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
}
static void path_get(wchar_t *buf, DWORD n) {
    DWORD sz = n;
    LONG r = RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"Path",
                          RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ, NULL, buf, &sz);
    if (r != ERROR_SUCCESS) buf[0] = L'\0';
    buf[n-1] = L'\0';
}
static int path_has(const wchar_t *dir) {
    wchar_t p[8192]; path_get(p, 8192);
    int dl = (int)wcslen(dir), pl = (int)wcslen(p), i = 0;
    while (i <= pl) {
        int j = i; while (j < pl && p[j] != L';') j++;
        if (j - i == dl && _wcsnicmp(p+i, dir, dl) == 0) return 1;
        i = j + 1;
    }
    return 0;
}
static void path_add(const wchar_t *dir) {
    if (path_has(dir)) return;
    wchar_t p[8192]; path_get(p, 8192);
    wchar_t np[16384];
    if (p[0]) wsprintfW(np, L"%s;%s", p, dir); else wsprintfW(np, L"%s", dir);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Environment", L"Path", REG_EXPAND_SZ, np, (DWORD)(wcslen(np)+1)*2);
    notify();
}
static void path_remove(const wchar_t *dir) {
    wchar_t p[8192]; path_get(p, 8192);
    wchar_t out[16384]; int o = 0, dl = (int)wcslen(dir), pl = (int)wcslen(p), i = 0;
    while (i <= pl) {
        int j = i; while (j < pl && p[j] != L';') j++;
        if (!(j - i == dl && _wcsnicmp(p+i, dir, dl) == 0)) {
            if (o && out[o-1] != L';') out[o++] = L';';
            memcpy(out+o, p+i, (size_t)(j-i)*2); o += j-i;
        }
        i = j + 1;
    }
    out[o] = L'\0';
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Environment", L"Path", REG_EXPAND_SZ, out, (DWORD)(o+1)*2);
    notify();
}

/* ── Асоціація .sokil ── */
static void assoc_add(const wchar_t *exe) {
    wchar_t cmd[1024];
    wsprintfW(cmd, L"\"%s\" \"%%1\"", exe);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes", L".sokil", REG_SZ, L"SokilScript", 0);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript", NULL, REG_SZ, L"Сокіл Скрипт", 0);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript\\shell\\open\\command", NULL, REG_SZ, cmd, 0);
}
static void assoc_del(void) {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript");
}
static int assoc_has(void) {
    DWORD sz = 0;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil", NULL, RRF_RT_REG_SZ, NULL, NULL, &sz) == ERROR_SUCCESS;
}

/* ── Install / Update / Uninstall ── */
static BOOL do_install(int patch) {
    if (!CreateDirectoryW(APP_DIR, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    wchar_t exe[MAX_PATH];
    wsprintfW(exe, L"%s\\sokil.exe", APP_DIR);
    HANDLE h = CreateFileW(exe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w = 0;
    WriteFile(h, sokil_exe_data, sokil_exe_len, &w, NULL);
    CloseHandle(h);
    if (w != sokil_exe_len) return FALSE;
    if (patch)
        path_add(APP_DIR);
    assoc_add(exe);
    return TRUE;
}
static BOOL do_uninstall(void) {
    assoc_del();
    path_remove(APP_DIR);
    wchar_t exe[MAX_PATH];
    wsprintfW(exe, L"%s\\sokil.exe", APP_DIR);
    DeleteFileW(exe);
    RemoveDirectoryW(APP_DIR);
    return TRUE;
}

/* ═══════════ Custom кнопка M3 ═══════════ */
typedef struct { BOOL hover, down, primary; wchar_t label[32]; } BtnSt;
static void b_round_fill(HDC hdc, RECT *rc, COLORREF col, int r) {
    HBRUSH br = CreateSolidBrush(col);
    HRGN rg = CreateRoundRectRgn(rc->left, rc->top, rc->right+1, rc->bottom+1, r*2, r*2);
    FillRgn(hdc, rg, br);
    HGDIOBJ old = SelectObject(hdc, (HGDIOBJ)br);
    FrameRgn(hdc, rg, br, 1, 1);
    SelectObject(hdc, old);
    DeleteObject(rg); DeleteObject(br);
}
static LRESULT CALLBACK M3BtnProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    BtnSt *st = (BtnSt *)GetWindowLongPtrW(hw, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        BtnSt *s = (BtnSt *)calloc(1, sizeof(BtnSt));
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        s->primary = (cs->hMenu == (HMENU)BTN_INSTALL || cs->hMenu == (HMENU)BTN_UPDATE);
        if (cs->lpszName) {
            wcsncpy(s->label, cs->lpszName, 31);
            s->label[31] = L'\0';
        }
        SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)s);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc; GetClientRect(hw, &rc);
        COLORREF bg, fg;
        if (st->primary) {
            bg = st->down ? M3_PRIMARY_DARK : st->hover ? M3_PRIMARY_LT : M3_PRIMARY;
            fg = RGB(0xFF,0xFF,0xFF);
        } else {
            bg = st->down ? RGB(0xD0,0xBC,0xE8) : st->hover ? RGB(0xF0,0xE6,0xFF) : M3_TONAL;
            fg = M3_TONAL_TXT;
        }
        b_round_fill(hdc, &rc, bg, 8);
        HFONT f = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        HFONT old = (HFONT)SelectObject(hdc, f);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);
        DrawTextW(hdc, st->label, -1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(f);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        st->down = TRUE;
        SetCapture(hw);
        InvalidateRect(hw, NULL, TRUE);
        return 0;
    case WM_SETTEXT:
        if (st && lp) {
            wcsncpy(st->label, (const wchar_t *)lp, 31);
            st->label[31] = L'\0';
            InvalidateRect(hw, NULL, TRUE);
            return TRUE;
        }
        return FALSE;
    case WM_GETTEXTLENGTH:
        return st ? (LRESULT)wcslen(st->label) : 0;
    case WM_GETTEXT:
        if (st && lp && wp > 0) {
            wcsncpy((wchar_t *)lp, st->label, wp - 1);
            ((wchar_t *)lp)[wp - 1] = L'\0';
            return (LRESULT)wcslen(st->label);
        }
        return 0;
    case WM_LBUTTONUP: {
        st->down = FALSE;
        ReleaseCapture();
        InvalidateRect(hw, NULL, TRUE);
        RECT rc; GetClientRect(hw, &rc);
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (PtInRect(&rc, pt)) {
            DWORD_PTR id = (DWORD_PTR)GetMenu(hw);
            SendMessageW(GetParent(hw), WM_COMMAND, MAKEWPARAM((WORD)id, 0), 0);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!st->hover) {
            st->hover = TRUE;
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hw, 0 };
            TrackMouseEvent(&t);
            InvalidateRect(hw, NULL, TRUE);
        }
        return 0;
    case WM_MOUSELEAVE:
        st->hover = FALSE;
        InvalidateRect(hw, NULL, TRUE);
        return 0;
    case WM_DESTROY:
        free(st);
        SetWindowLongPtrW(hw, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ═══════════ Головне вікно ═══════════ */
static HFONT fTitle, fBody, fSmall;
static wchar_t statusTxt[512];
static COLORREF statusCol = M3_MUTED;

static void set_status(const wchar_t *txt, COLORREF col) {
    wsprintfW(statusTxt, L"%s", txt);
    statusCol = col;
    InvalidateRect(hMain, NULL, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        hMain = hw;
        fTitle = CreateFontW(26, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fBody  = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fSmall = CreateFontW(10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        hbrCard = CreateSolidBrush(M3_CARD);
        hChkPath = CreateWindowW(L"BUTTON", L"Патчити PATH (додати до PATH)",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP,
            38, 164, 300, 26, hw, NULL, NULL, NULL);
        SendMessageW(hChkPath, WM_SETFONT, (WPARAM)fBody, TRUE);
        SendMessageW(hChkPath, BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowW(L"M3Btn", L"Встановити",
            WS_CHILD|WS_VISIBLE, 24, 210, 128, 44, hw, (HMENU)BTN_INSTALL, NULL, NULL);
        CreateWindowW(L"M3Btn", L"Оновити",
            WS_CHILD|WS_VISIBLE, 160, 210, 128, 44, hw, (HMENU)BTN_UPDATE, NULL, NULL);
        CreateWindowW(L"M3Btn", L"Видалити",
            WS_CHILD|WS_VISIBLE, 296, 210, 120, 44, hw, (HMENU)BTN_UNINSTALL, NULL, NULL);
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            DWORD pref = 2;
            HRESULT (WINAPI *fn)(HWND, DWORD, LPCVOID, DWORD) =
                (HRESULT (WINAPI *)(HWND,DWORD,LPCVOID,DWORD))GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (fn) fn(hw, 33, &pref, sizeof pref);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case BTN_INSTALL: {
            BOOL patch = SendMessageW(hChkPath, BM_GETCHECK, 0, 0) == BST_CHECKED;
            BOOL ok = do_install(patch);
            set_status(ok ? L"✓ Встановлено! Новий термінал →  sokil файл.sokil"
                          : L"✗ Помилка встановлення", ok ? M3_OK : M3_ERR);
            InvalidateRect(hw, NULL, TRUE);
            break;
        }
        case BTN_UPDATE: {
            BOOL patch = SendMessageW(hChkPath, BM_GETCHECK, 0, 0) == BST_CHECKED;
            BOOL ok = do_install(patch);
            set_status(ok ? L"✓ Оновлено! Бінарник, PATH та .sokil перевірено"
                          : L"✗ Помилка оновлення", ok ? M3_OK : M3_ERR);
            InvalidateRect(hw, NULL, TRUE);
            break;
        }
        case BTN_UNINSTALL:
            do_uninstall();
            set_status(L"✓ Видалено. PATH та асоціацію .sokil прибрано", M3_OK);
            InvalidateRect(hw, NULL, TRUE);
            break;
        }
        return 0;
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hw, &rc);
        HBRUSH br = CreateSolidBrush(M3_SURFACE);
        FillRect((HDC)wp, &rc, br);
        DeleteObject(br);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        SetBkMode(hdc, TRANSPARENT);
        RECT rc;
        HGDIOBJ old = SelectObject(hdc, fTitle);
        SetTextColor(hdc, M3_PRIMARY_TXT);
        rc = (RECT){24, 18, 400, 54};
        DrawTextW(hdc, L"Сокіл", -1, &rc, DT_LEFT|DT_SINGLELINE);
        SelectObject(hdc, fBody);
        SetTextColor(hdc, M3_MUTED);
        rc = (RECT){24, 52, 400, 74};
        DrawTextW(hdc, L"Мова програмування · Інсталятор", -1, &rc, DT_LEFT|DT_SINGLELINE);

        RECT card = {24, 86, 416, 196};
        b_round_fill(hdc, &card, M3_CARD, 12);
        SetTextColor(hdc, M3_ON_SURFACE);
        wchar_t line[1024];
        wsprintfW(line, L"Каталог        %s", APP_DIR);
        rc = (RECT){38, 100, 402, 124}; DrawTextW(hdc, line, -1, &rc, DT_LEFT|DT_SINGLELINE);
        wsprintfW(line, L"Файли .sokil   %s", assoc_has() ? L"так" : L"ні");
        rc = (RECT){38, 132, 402, 156}; DrawTextW(hdc, line, -1, &rc, DT_LEFT|DT_SINGLELINE);
        SelectObject(hdc, old);

        SelectObject(hdc, fBody);
        SetTextColor(hdc, statusCol);
        rc = (RECT){24, 268, 416, 296};
        DrawTextW(hdc, statusTxt, -1, &rc, DT_LEFT|DT_SINGLELINE);

        SelectObject(hdc, fSmall);
        SetTextColor(hdc, M3_OUTLINE);
        rc = (RECT){24, 318, 416, 334};
        DrawTextW(hdc, L"Сокіл v2.2 · C99 · zero dependencies · MIT", -1, &rc, DT_LEFT|DT_SINGLELINE);
        SelectObject(hdc, old);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == hChkPath) {
            SetBkMode((HDC)wp, TRANSPARENT);
            SetTextColor((HDC)wp, M3_ON_SURFACE);
            return (LRESULT)hbrCard;
        }
        break;
    case WM_DESTROY:
        DeleteObject(fTitle); DeleteObject(fBody); DeleteObject(fSmall);
        if (hbrCard) DeleteObject(hbrCard);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static int gui_main(void) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"SokilSetup";
    RegisterClassExW(&wc);

    WNDCLASSEXW b = {0};
    b.cbSize = sizeof b;
    b.lpfnWndProc = M3BtnProc;
    b.hInstance = wc.hInstance;
    b.hCursor = LoadCursor(NULL, IDC_HAND);
    b.hbrBackground = NULL;
    b.lpszClassName = L"M3Btn";
    RegisterClassExW(&b);

    HWND hw = CreateWindowExW(0, L"SokilSetup", L"Сокіл — Встановлення",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 448, 372,
        NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hw, SW_SHOW);
    UpdateWindow(hw);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

int main(int argc, char **argv) {
    GetEnvironmentVariableW(L"LOCALAPPDATA", APP_DIR, MAX_PATH);
    if (APP_DIR[0] == L'\0') {
        GetEnvironmentVariableW(L"USERPROFILE", APP_DIR, MAX_PATH);
        wcscat_s(APP_DIR, MAX_PATH, L"\\AppData\\Local");
    }
    wcscat_s(APP_DIR, MAX_PATH, L"\\Sokil");
    if (argc > 1) {
        if (!strcmp(argv[1], "--install"))   return do_install(1) ? 0 : 1;
        if (!strcmp(argv[1], "--uninstall")) return do_uninstall() ? 0 : 1;
        if (!strcmp(argv[1], "--version"))   { printf("Sokil Setup v2.3\n"); return 0; }
        if (!strcmp(argv[1], "--path"))      { printf("%S\n", APP_DIR); return 0; }
    }
    return gui_main();
}