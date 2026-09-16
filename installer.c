/*
 * Сокіл — інсталятор-майстер (wizard), дизайн Material 3.
 * 100% власний код на C (Win32 API), без залежностей.
 *
 * v2.6: wizard (Вітання → Шлях → Готово), анімований фон з кодом,
 *       встановлює sokil.exe + SokilIDE.exe автоматично,
 *       асоціація .sokil через cmd /k, вибір версії з GitHub.
 */
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <urlmon.h>
#include <stdio.h>
#include <string.h>

#include "sokil_exe.h"
#include "sokil_ide.h"

#define BTN_NEXT    1001
#define BTN_BACK    1002
#define BTN_CLOSE   1003
#define BTN_INSTALL 1004
#define BTN_UPDATE  1005
#define BTN_UNINST  1006
#define BTN_BROWSE  1007

/* ── Material 3 кольори ── */
#define M3_PRIMARY      0x006750A4
#define M3_PRIMARY_LT   0x00EADDFF
#define M3_PRIMARY_DARK 0x00381E72
#define M3_SURFACE      0x00FFFBFE
#define M3_CARD         0x00F7F2FA
#define M3_TONAL        0x00E8DEF8
#define M3_TONAL_TXT    0x004A4458
#define M3_ON_SURFACE   0x001D1B20
#define M3_MUTED        0x0079747E
#define M3_OUTLINE      0x00CAC4D0
#define M3_GREEN        0x00386A20

static wchar_t APP_DIR[MAX_PATH];
static HWND hMain, hChkPath, hCombo, hPath;
static HFONT fTitle, fSubtitle, fBody, fSmall, fMono, fIcon;
static HBRUSH hbrCard, hbrSurface;
static wchar_t statusTxt[512];
static int g_page = 0;
static int g_anim = 0;
static wchar_t CUR_VER[] = L"2.11";

#define MAX_VER 16
static wchar_t ver_urls[MAX_VER][768];
static int ver_count = 0;

#define RELS_URL L"https://api.github.com/repos/DenisVJR1/sokil-lang/releases?per_page=12"
#define LATEST   L"https://github.com/DenisVJR1/sokil-lang/releases/latest/download/sokil.exe"

/* ── Анімований фон-код ── */
static const wchar_t *BG_CODE[] = {
    L"print('Сокіл')",
    L"let a = [1,2,3]; sort(a)",
    L"fn hello(name) {",
    L"  return 'Привіт, ' + name",
    L"}",
    L"for i in range(5) { print(i) }",
    L"sokil --compile app.sokil",
    L"let x = 42",
    L"random(1, 100)",
    L"input('як тебе звати? ')",
    L"while x > 0 { x = x - 1 }",
    L"print(len('Сокіл'))",
    L"let arr = push([1,2], 3)",
    L"print('hi' * 5)",
    L"print(now() > 1000000000)",
};
#define BG_N (int)(sizeof BG_CODE / sizeof BG_CODE[0])

/* forward */
static void notify_env(void);

/* ── PATH ── */
static void path_add(void) {
    DWORD sz = 32767;
    wchar_t *old = (wchar_t *)malloc(sz * sizeof(wchar_t));
    if (!old) return;
    LONG r = RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"Path",
                          RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_SZ, NULL, old, &sz);
    if (r != ERROR_SUCCESS) { wcscpy(old, L"%USERPROFILE%\\AppData\\Local\\Microsoft\\WindowsApps"); }
    if (wcsstr(old, APP_DIR) != NULL) { free(old); return; }
    /* normalize: strip leading/trailing ';' */
    wchar_t *p = old;
    while (*p == L';') p++;
    size_t len = wcslen(p);
    while (len && p[len - 1] == L';') p[--len] = 0;
    if (len == 0) wcscpy(p, L"%USERPROFILE%\\AppData\\Local\\Microsoft\\WindowsApps");
    size_t n = wcslen(p) + wcslen(APP_DIR) + 2;
    wchar_t *nw = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (!nw) { free(old); return; }
    wsprintfW(nw, L"%s;%s", p, APP_DIR);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Environment", L"Path", REG_EXPAND_SZ, nw, (DWORD)((wcslen(nw)+1)*sizeof(wchar_t)));
    free(nw); free(old);
    notify_env();
}
static void path_remove(void) {
    DWORD sz = 32767;
    wchar_t *old = (wchar_t *)malloc(sz * sizeof(wchar_t));
    if (!old) return;
    LONG r = RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"Path",
                          RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_SZ, NULL, old, &sz);
    if (r != ERROR_SUCCESS) { free(old); return; }
    wchar_t *hit = wcsstr(old, APP_DIR);
    if (hit) {
        wchar_t *sep = hit - 1;
        if (sep >= old && *sep == L';')
            memmove(sep, hit + wcslen(APP_DIR), (wcslen(hit + wcslen(APP_DIR)) + 1) * sizeof(wchar_t));
        else
            memmove(hit, hit + wcslen(APP_DIR), (wcslen(hit + wcslen(APP_DIR)) + 1) * sizeof(wchar_t));
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Environment", L"Path", REG_EXPAND_SZ, old, (DWORD)((wcslen(old)+1)*sizeof(wchar_t)));
    }
    free(old);
}
static void notify_env(void) {
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment",
                        SMTO_ABORTIFHUNG, 4000, NULL);
}

/* ── Асоціація .sokil → cmd /k ── */
static void assoc_add(const wchar_t *exe) {
    wchar_t cmd[1024];
    wsprintfW(cmd, L"cmd.exe /k \"\"%s\" \"%%1\"\"", exe);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil", NULL, REG_SZ, L"SokilScript", 12 * sizeof(wchar_t));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil\\DefaultIcon", NULL, REG_SZ, exe, (DWORD)((wcslen(exe)+1)*sizeof(wchar_t)));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript\\shell\\open\\command", NULL, REG_SZ, cmd, (DWORD)((wcslen(cmd)+1)*sizeof(wchar_t)));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript\\DefaultIcon", NULL, REG_SZ, exe, (DWORD)((wcslen(exe)+1)*sizeof(wchar_t)));
}
static void assoc_del(void) {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript");
}

/* ── Встановлення / Видалення ── */
static BOOL write_bytes(const wchar_t *path, const unsigned char *data, unsigned int len) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w = 0;
    WriteFile(h, data, len, &w, NULL);
    CloseHandle(h);
    return w == len;
}
static BOOL download_to(const wchar_t *url, const wchar_t *dest) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(tmp + wcslen(tmp), L"sokil-dl-%lu.exe", GetCurrentProcessId());
    HRESULT hr = URLDownloadToFileW(NULL, url, tmp, 0, NULL);
    if (FAILED(hr)) return FALSE;
    FILE *f = _wfopen(tmp, L"rb");
    unsigned char mz[2] = {0};
    if (f) { fread(mz, 1, 2, f); fclose(f); }
    if (mz[0] != 'M' || mz[1] != 'Z') { DeleteFileW(tmp); return FALSE; }
    BOOL ok = MoveFileExW(tmp, dest, MOVEFILE_REPLACE_EXISTING);
    if (!ok) DeleteFileW(tmp);
    SetFileAttributesW(dest, FILE_ATTRIBUTE_NORMAL);
    return ok;
}
static BOOL do_install(int patch, int version) {
    if (!CreateDirectoryW(APP_DIR, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;

    /* sokil.exe */
    wchar_t exe[MAX_PATH], ide[MAX_PATH];
    wsprintfW(exe, L"%s\\sokil.exe", APP_DIR);
    wsprintfW(ide, L"%s\\SokilIDE.exe", APP_DIR);

    BOOL ok;
    if (version == 0) {
        ok = write_bytes(exe, sokil_exe_data, sokil_exe_len);
    } else if (version == 1) {
        ok = download_to(LATEST, exe);
    } else if (version - 2 < ver_count && ver_urls[version - 2][64]) {
        ok = download_to(ver_urls[version - 2] + 64, exe);
    } else return FALSE;
    if (!ok) return FALSE;

    /* SokilIDE.exe — завжди з вбудованого */
    write_bytes(ide, sokil_ide_data, sokil_ide_len);

    if (patch) path_add();
    assoc_add(exe);
    return TRUE;
}
static BOOL do_uninstall(void) {
    assoc_del();
    path_remove();
    notify_env();
    wchar_t exe[MAX_PATH], ide[MAX_PATH];
    wsprintfW(exe, L"%s\\sokil.exe", APP_DIR);
    wsprintfW(ide, L"%s\\SokilIDE.exe", APP_DIR);
    DeleteFileW(exe);
    DeleteFileW(ide);
    RemoveDirectoryW(APP_DIR);
    return TRUE;
}

/* ── Малювання ── */
static void round_fill(HDC hdc, RECT *rc, COLORREF col, int r) {
    HBRUSH br = CreateSolidBrush(col);
    HRGN rg = CreateRoundRectRgn(rc->left, rc->top, rc->right + 1, rc->bottom + 1, r * 2, r * 2);
    FillRgn(hdc, rg, br);
    DeleteObject(rg);
    DeleteObject(br);
}
static void set_status(const wchar_t *txt) {
    wsprintfW(statusTxt, L"%s", txt);
    if (hMain) InvalidateRect(hMain, NULL, TRUE);
}

/* ── GitHub API ── */
static void fetch_versions(void) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(tmp + wcslen(tmp), L"sokil-rel.json");
    if (FAILED(URLDownloadToFileW(NULL, RELS_URL, tmp, 0, NULL))) return;
    FILE *f = _wfopen(tmp, L"rb");
    DeleteFileW(tmp);
    if (!f) return;
    long sz; fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1 << 20) { fclose(f); return; }
    char *js = (char *)malloc((size_t)sz + 1);
    if (!js) { fclose(f); return; }
    fread(js, 1, (size_t)sz, f); js[sz] = '\0'; fclose(f);
    ver_count = 0;
    const char *p = js;
    while (ver_count < MAX_VER) {
        const char *tag = strstr(p, "\"tag_name\":\"");
        if (!tag) break;
        tag += 12;
        const char *tag_end = strchr(tag, '"');
        if (!tag_end) break;
        const char *next = strstr(tag_end, "\"tag_name\":\"");
        size_t win = next ? (size_t)(next - tag_end) : (size_t)(js + sz - tag_end);
        if (win > 8000) win = 8000;
        const char *q = tag_end;
        const char *wstop = tag_end + (win < 6000 ? win : 6000);
        while (q < wstop && q[1]) {
            const char *nm = strstr(q, "\"name\":\"sokil");
            if (!nm || nm >= wstop) break;
            const char *du = strstr(nm, "\"browser_download_url\":\"");
            if (du && du < wstop) {
                const char *du_end = strchr(du + 24, '"');
                if (du_end) {
                    int tl = (int)(tag_end - tag), ul = (int)(du_end - (du + 24));
                    if (tl > 0 && tl < 64 && ul > 0 && ul < 700) {
                        char t[64], u[700];
                        memcpy(t, tag, tl); t[tl] = 0;
                        memcpy(u, du + 24, ul); u[ul] = 0;
                        MultiByteToWideChar(CP_UTF8, 0, t, -1, ver_urls[ver_count], 64);
                        MultiByteToWideChar(CP_UTF8, 0, u, -1, ver_urls[ver_count] + 64, 700);
                        ver_count++;
                    }
                }
                break;
            }
            q = nm + 1;
        }
        p = (next ? next : tag_end);
    }
    free(js);
}

/* ── Сторінки майстра ── */
static void set_page(int page) {
    g_page = page;
    HWND ctrls[] = { hPath, hChkPath, hCombo };
    for (int i = 0; i < 3; i++)
        if (ctrls[i]) ShowWindow(ctrls[i], (page == 1) ? SW_SHOW : SW_HIDE);
    static const int ids[] = { BTN_NEXT, BTN_BACK, BTN_CLOSE, BTN_INSTALL, BTN_UPDATE, BTN_UNINST, BTN_BROWSE };
    for (int i = 0; i < 7; i++) {
        HWND b = GetDlgItem(hMain, ids[i]);
        if (!b) continue;
        BOOL vis = FALSE;
        switch (ids[i]) {
            case BTN_NEXT:    vis = page == 0; break;
            case BTN_BACK:    vis = page == 1; break;
            case BTN_CLOSE:   vis = page == 2; break;
            case BTN_INSTALL: vis = page == 1; break;
            case BTN_UPDATE:  vis = page == 1; break;
            case BTN_UNINST:  vis = page == 0 || page == 1; break;
            case BTN_BROWSE:  vis = page == 1; break;
        }
        ShowWindow(b, vis ? SW_SHOW : SW_HIDE);
    }
    InvalidateRect(hMain, NULL, TRUE);
    UpdateWindow(hMain);
}

static void browse_dir(HWND hw) {
    BROWSEINFOW bi = {0};
    bi.hwndOwner = hw;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpszTitle = L"Оберіть каталог для встановлення Сокола";
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t buf[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, buf))
            SetWindowTextW(hPath, buf);
        CoTaskMemFree(pidl);
    }
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        hMain = hw;
        fTitle    = CreateFontW(32, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI Variable Display");
        fSubtitle = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fBody     = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fSmall    = CreateFontW(10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fMono     = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");
        fIcon     = CreateFontW(42, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI Variable Display");
        hbrCard    = CreateSolidBrush(M3_CARD);
        hbrSurface = CreateSolidBrush(M3_SURFACE);

        /* Сторінка 1: шлях + версія */
        CreateWindowW(L"STATIC", L"Каталог встановлення:", WS_CHILD|WS_VISIBLE, 44, 108, 340, 20, hw, NULL, NULL, NULL);
        hPath = CreateWindowW(L"EDIT", APP_DIR, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 44, 130, 310, 28, hw, NULL, NULL, NULL);
        SendMessageW(hPath, WM_SETFONT, (WPARAM)fBody, TRUE);
        CreateWindowW(L"BUTTON", L"Огляд...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 362, 130, 78, 28, hw, (HMENU)BTN_BROWSE, NULL, NULL);

        hChkPath = CreateWindowW(L"BUTTON", L"Патчити PATH — запуск з будь-якого терміналу",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 44, 170, 380, 24, hw, NULL, NULL, NULL);
        SendMessageW(hChkPath, WM_SETFONT, (WPARAM)fBody, TRUE);
        SendMessageW(hChkPath, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindowW(L"STATIC", L"Версія:", WS_CHILD|WS_VISIBLE, 44, 200, 340, 18, hw, NULL, NULL, NULL);
        hCombo = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD|WS_VISIBLE|WS_VSCROLL|CBS_DROPDOWNLIST, 44, 222, 330, 220, hw, NULL, NULL, NULL);
        SendMessageW(hCombo, WM_SETFONT, (WPARAM)fBody, TRUE);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Вбудована (офлайн)");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Остання з GitHub");
        fetch_versions();
        for (int i = 0; i < ver_count; i++)
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)ver_urls[i]);
        SendMessageW(hCombo, CB_SETCURSEL, 2, 0);

        /* Кнопки (owner-draw, rounded M3) */
        CreateWindowW(L"BUTTON", L"Далі →",    WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 24, 350, 136, 44, hw, (HMENU)BTN_NEXT, NULL, NULL);
        CreateWindowW(L"BUTTON", L"← Назад",   WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 24, 350, 136, 44, hw, (HMENU)BTN_BACK, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Закрити",   WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 168, 350, 136, 44, hw, (HMENU)BTN_CLOSE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Встановити", WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 168, 350, 148, 44, hw, (HMENU)BTN_INSTALL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Оновити",   WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 24, 350, 136, 44, hw, (HMENU)BTN_UPDATE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Видалити",  WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 326, 350, 120, 44, hw, (HMENU)BTN_UNINST, NULL, NULL);

        SetTimer(hw, 1, 130, NULL);

        /* Dark title bar */
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            DWORD pref = 2;
            HRESULT (WINAPI *fn)(HWND, DWORD, LPCVOID, DWORD) =
                (HRESULT (WINAPI *)(HWND,DWORD,LPCVOID,DWORD))GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (fn) fn(hw, 33, &pref, sizeof pref);
        }
        set_page(0);
        return 0;
    }
    case WM_TIMER:
        g_anim++;
        InvalidateRect(hw, NULL, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        SetBkMode(hdc, TRANSPARENT);
        RECT rc;
        GetClientRect(hw, &rc);
        FillRect(hdc, &rc, hbrSurface);

        /* анімований фон-код (туманний) */
        SelectObject(hdc, fMono);
        SetTextColor(hdc, 0x00D8D0E8);
        for (int i = 0; i < BG_N; i++) {
            int speed = 14 + (i % 3) * 7;
            int x = 340 - (g_anim * speed) % 680;
            int y = 90 + i * 18;
            if (x > -160 && x < 486) {
                RECT lr = {x, y, x + 300, y + 16};
                DrawTextW(hdc, BG_CODE[i], -1, &lr, DT_LEFT|DT_SINGLELINE);
            }
        }

        /* ── Заголовок ── */
        HGDIOBJ old = SelectObject(hdc, fIcon);
        SetTextColor(hdc, M3_PRIMARY);
        rc = (RECT){24, 12, 460, 56};
        DrawTextW(hdc, L"Сокіл", -1, &rc, DT_LEFT|DT_SINGLELINE);
        SelectObject(hdc, fSubtitle);
        SetTextColor(hdc, M3_MUTED);
        rc = (RECT){24, 54, 460, 72};
        wchar_t sub[128];
        wsprintfW(sub, L"Мова програмування · Інсталятор v%s", CUR_VER);
        DrawTextW(hdc, sub, -1, &rc, DT_LEFT|DT_SINGLELINE);

        /* горизонтальна лінія-розділювач */
        HPEN pen = CreatePen(PS_SOLID, 1, M3_OUTLINE);
        HPEN oldp = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, 24, 76, NULL);
        LineTo(hdc, 456, 76);
        SelectObject(hdc, oldp);
        DeleteObject(pen);

        if (g_page == 0) {
            /* вітання */
            SetTextColor(hdc, M3_ON_SURFACE);
            SelectObject(hdc, fBody);
            rc = (RECT){38, 92, 450, 330};
            DrawTextW(hdc,
                L"Ласкаво просимо до встановлення Сокола!\n\n"
                L"Встановлення включає:\n"
                L"  \x2022  sokil — інтерпретатор мови\n"
                L"  \x2022  SokilIDE — редактор з підсвіткою\n"
                L"  \x2022  Асоціація .sokil (подвійний клік = запуск)\n\n"
                L"sokil --compile \x2192 файл.exe\n"
                L"  (власна компіляція БЕЗ компілятора C)\n\n"
                L"Натисніть «Далі», щоб обрати каталог і версію.",
                -1, &rc, DT_LEFT|DT_WORDBREAK);
        } else if (g_page == 1) {
            /* картка для полів */
            RECT card = {24, 86, 456, 275};
            round_fill(hdc, &card, M3_CARD, 12);
        } else {
            /* готово */
            SetTextColor(hdc, M3_GREEN);
            SelectObject(hdc, fIcon);
            rc = (RECT){38, 88, 450, 130};
            DrawTextW(hdc, statusTxt[0] && statusTxt[0] == L'\x2713' ? L"\x2713" : L"\x2717", -1, &rc, DT_LEFT|DT_SINGLELINE);
            SetTextColor(hdc, M3_ON_SURFACE);
            SelectObject(hdc, fBody);
            rc = (RECT){38, 140, 450, 340};
            DrawTextW(hdc, statusTxt[0] ? statusTxt : L"Готово", -1, &rc, DT_LEFT|DT_WORDBREAK);
        }

        /* footer */
        SelectObject(hdc, fSmall);
        SetTextColor(hdc, M3_MUTED);
        rc = (RECT){24, 402, 460, 420};
        {
            wchar_t foot[256];
            wsprintfW(foot, L"Сокіл v%s \x00B7 C99 \x00B7 zero dependencies \x00B7 MIT \x00B7 github.com/DenisVJR1/sokil-lang", CUR_VER);
            DrawTextW(hdc, foot, -1, &rc, DT_LEFT|DT_SINGLELINE);
        }
        SelectObject(hdc, old);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlType != ODT_BUTTON) break;
        RECT rc = di->rcItem;
        COLORREF bg, fg;
        BOOL down = (di->itemState & ODS_SELECTED) != 0;
        if (di->CtlID == BTN_UNINST || di->CtlID == BTN_BACK) {
            bg = down ? 0x00D0C0E0 : M3_TONAL;
            fg = M3_TONAL_TXT;
        } else {
            bg = down ? M3_PRIMARY_DARK : M3_PRIMARY;
            fg = 0x00FFFFFF;
        }
        round_fill(di->hDC, &rc, bg, 10);
        HFONT f = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        HGDIOBJ o = SelectObject(di->hDC, f);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, fg);
        wchar_t label[64];
        GetWindowTextW(di->hwndItem, label, 64);
        DrawTextW(di->hDC, label, -1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(di->hDC, o);
        DeleteObject(f);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, M3_ON_SURFACE);
        return (LRESULT)hbrCard;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case BTN_NEXT:  set_page(1); break;
        case BTN_BACK:  set_page(0); break;
        case BTN_CLOSE: DestroyWindow(hw); break;
        case BTN_BROWSE: browse_dir(hw); break;
        case BTN_INSTALL: case BTN_UPDATE: {
            wchar_t pth[MAX_PATH];
            GetWindowTextW(hPath, pth, MAX_PATH);
            if (pth[0]) wcscpy(APP_DIR, pth);
            BOOL patch = SendMessageW(hChkPath, BM_GETCHECK, 0, 0) == BST_CHECKED;
            int ver = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (ver < 0) ver = 0;
            BOOL ok = do_install(patch, ver);
            if (ok) {
                wchar_t msg[1024];
                wsprintfW(msg,
                    L"\x2713  Встановлення завершено!\n\n"
                    L"Каталог:    %s\n"
                    L"Файли:      sokil.exe + SokilIDE.exe\n\n"
                    L"Запуск:     sokil файл.sokil\n"
                    L"Компіляція: sokil --compile файл.sokil\n"
                    L"Оновлення:  sokil --update\n"
                    L"Середовище: запустіть SokilIDE.exe",
                    APP_DIR);
                set_status(msg);
            } else {
                set_status(L"\x2717  Помилка встановлення.\nПеревірте інтернет або оберіть вбудовану версію.");
            }
            set_page(2);
            break;
        }
        case BTN_UNINST: {
            if (do_uninstall())
                set_status(L"\x2713  Видалення завершено.\n\nСокіл прибрано повністю: файли, PATH, асоціація.");
            else
                set_status(L"\x2717  Помилка видалення.");
            set_page(2);
            break;
        }
        }
        break;
    case WM_DESTROY:
        KillTimer(hw, 1);
        DeleteObject(fTitle); DeleteObject(fSubtitle); DeleteObject(fBody);
        DeleteObject(fSmall); DeleteObject(fMono); DeleteObject(fIcon);
        if (hbrCard) DeleteObject(hbrCard);
        if (hbrSurface) DeleteObject(hbrSurface);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static int gui_main(HINSTANCE hInst) {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"SokilSetup";
    RegisterClassW(&wc);
    HWND hw = CreateWindowExW(0, L"SokilSetup", L"Сокіл — Встановлення",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 486, 458, NULL, NULL, hInst, NULL);
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

static void app_dir(void) {
    DWORD sz = GetEnvironmentVariableW(L"LOCALAPPDATA", APP_DIR, MAX_PATH);
    if (sz == 0 || sz >= MAX_PATH) wcscpy(APP_DIR, L"C:\\Users\\Public");
    size_t l = wcslen(APP_DIR);
    if (l && APP_DIR[l - 1] != L'\\') wcscat(APP_DIR, L"\\");
    wcscat(APP_DIR, L"Sokil");
}

int wmain(void) {
    app_dir();
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        int code;
        if (!wcscmp(argv[1], L"--install"))      code = do_install(1, 0) ? 0 : 1;
        else if (!wcscmp(argv[1], L"--uninstall")) code = do_uninstall() ? 0 : 1;
        else if (!wcscmp(argv[1], L"--version")) { printf("Sokil Setup v%s\n", "2.11"); return 0; }
        else if (!wcscmp(argv[1], L"--path"))    { printf("%S\n", APP_DIR); return 0; }
        else return 1;
        LocalFree(argv);
        return code;
    }
    LocalFree(argv);
    return gui_main(GetModuleHandleW(NULL));
}