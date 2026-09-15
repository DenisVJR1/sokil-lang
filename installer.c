/*
 * Сокіл — інсталятор у стилі Inno Setup, дизайн Material 3.
 * 100% власний код на C (Win32 API), без залежностей.
 *
 * v2.4: вибір версії для завантаження з GitHub, стабільні кнопки
 *       (стандартний BUTTON + owner-draw, без кастомних класів).
 */
#include <windows.h>
#include <dwmapi.h>
#include <urlmon.h>
#include <stdio.h>
#include <string.h>

#include "sokil_exe.h"

#define BTN_INSTALL   1001
#define BTN_UPDATE    1002
#define BTN_UNINSTALL 1003

#define M3_PRIMARY      0x00A45067   /* 6750A4 */
#define M3_PRIMARY_DARK 0x0058358B
#define M3_SURFACE      0x00F7FEFE
#define M3_CARD         0x00EDF7F3
#define M3_TONAL        0x00FFDDEA
#define M3_TONAL_TXT    0x004C2100
#define M3_ON_SURFACE   0x001D1B20
#define M3_MUTED        0x00797875
#define M3_OUTLINE      0x00797875
#define M3_OK           0x004F6E03
#define M3_ERR          0x001EB326
#define M3_TXT_OK       0x0000480A
#define M3_TXT_ERR      0x00730A00

static wchar_t APP_DIR[MAX_PATH];
static HWND hMain;
static HWND hChkPath, hCombo;
static HFONT fTitle, fBody, fSmall;
static HBRUSH hbrCard, hbrSurface;
static wchar_t statusTxt[512];
static COLORREF statusCol = M3_ON_SURFACE;

#define MAX_VER 16
static wchar_t ver_urls[MAX_VER][768];
static int ver_count = 0;

#define RELS_URL L"https://api.github.com/repos/DenisVJR1/sokil-lang/releases?per_page=12"
#define DL_URL   L"https://github.com/DenisVJR1/sokil-lang/releases/download/%s/sokil.exe"
#define LATEST   L"https://github.com/DenisVJR1/sokil-lang/releases/latest/download/sokil.exe"

/* ── PATH у HKCU\Environment ── */
static int path_has(void) {
    DWORD sz = 32767;
    wchar_t *old = (wchar_t *)malloc(sz * sizeof(wchar_t));
    if (!old) return 0;
    LONG r = RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"Path",
                          RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_SZ, NULL, old, &sz);
    if (r != ERROR_SUCCESS) sz = 0;
    wchar_t *hay = (sz > 0) ? old : L"";
    int hit = (wcsstr(hay, APP_DIR) != NULL);
    free(old);
    return hit;
}
static void path_add(void) {
    DWORD sz = 32767;
    wchar_t *old = (wchar_t *)malloc(sz * sizeof(wchar_t));
    if (!old) return;
    LONG r = RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"Path",
                          RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_SZ, NULL, old, &sz);
    if (r != ERROR_SUCCESS) { wcscpy(old, L"%USERPROFILE%\\AppData\\Local\\Microsoft\\WindowsApps"); sz = wcslen(old) * sizeof(wchar_t); }
    if (wcsstr(old, APP_DIR) != NULL) goto done;
    {
        size_t n = wcslen(old) + wcslen(APP_DIR) + 2;
        wchar_t *nw = (wchar_t *)malloc(n * sizeof(wchar_t));
        if (!nw) goto done;
        wsprintfW(nw, L"%s;%s", old, APP_DIR);
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Environment", L"Path", REG_EXPAND_SZ, nw, (DWORD)((wcslen(nw)+1)*sizeof(wchar_t)));
        free(nw);
    }
done:
    free(old);
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
        if (sep >= old && *sep == L';') { memmove(sep, hit + wcslen(APP_DIR), (wcslen(hit + wcslen(APP_DIR)) + 1) * sizeof(wchar_t)); }
        else { memmove(hit, hit + wcslen(APP_DIR), (wcslen(hit + wcslen(APP_DIR)) + 1) * sizeof(wchar_t)); }
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Environment", L"Path", REG_EXPAND_SZ, old, (DWORD)((wcslen(old)+1)*sizeof(wchar_t)));
    }
    free(old);
}
static void notify_env(void) {
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment",
                        SMTO_ABORTIFHUNG, 4000, NULL);
}

/* ── Асоціація .sokil ── */
static void assoc_add(const wchar_t *exe) {
    wchar_t cmd[1024];
    wsprintfW(cmd, L"\"%s\" \"%%1\"", exe);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil", NULL, REG_SZ, L"SokilScript", 12 * sizeof(wchar_t));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil\\DefaultIcon", NULL, REG_SZ, exe, (DWORD)((wcslen(exe)+1)*sizeof(wchar_t)));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript\\shell\\open\\command", NULL, REG_SZ, cmd, (DWORD)((wcslen(cmd)+1)*sizeof(wchar_t)));
}
static void assoc_del(void) {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript");
}
static int assoc_has(void) {
    HKEY k;
    return RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil", 0, KEY_READ, &k) == ERROR_SUCCESS;
}

/* ── Install / Update / Uninstall ── */
static BOOL write_embedded(void) {
    wchar_t exe[MAX_PATH];
    wsprintfW(exe, L"%s\\sokil.exe", APP_DIR);
    HANDLE h = CreateFileW(exe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w = 0;
    WriteFile(h, sokil_exe_data, sokil_exe_len, &w, NULL);
    CloseHandle(h);
    return w == sokil_exe_len;
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
/* version: 0 = вбудована, 1 = latest, 2+ = тег з репозиторію */
static BOOL do_install(int patch, int version) {
    if (!CreateDirectoryW(APP_DIR, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    wchar_t exe[MAX_PATH];
    wsprintfW(exe, L"%s\\sokil.exe", APP_DIR);
    BOOL ok;
    if (version == 0) ok = write_embedded();
    else if (version == 1) ok = download_to(LATEST, exe);
    else if (version - 2 < ver_count && ver_urls[version - 2][64])
        ok = download_to(ver_urls[version - 2] + 64, exe);
    else return FALSE;
    if (!ok) return FALSE;
    if (patch) path_add();
    assoc_add(exe);
    return TRUE;
}
static BOOL do_uninstall(void) {
    assoc_del();
    path_remove();
    notify_env();
    wchar_t exe[MAX_PATH];
    wsprintfW(exe, L"%s\\sokil.exe", APP_DIR);
    DeleteFileW(exe);
    RemoveDirectoryW(APP_DIR);
    return TRUE;
}

/* ── Спільне малювання ── */
static void b_round_fill(HDC hdc, RECT *rc, COLORREF col, int r) {
    HBRUSH br = CreateSolidBrush(col);
    HRGN rg = CreateRoundRectRgn(rc->left, rc->top, rc->right + 1, rc->bottom + 1, r * 2, r * 2);
    FillRgn(hdc, rg, br);
    FrameRgn(hdc, rg, br, 1, 1);
    DeleteObject(rg);
    DeleteObject(br);
}
static void set_status(const wchar_t *txt, COLORREF col) {
    wsprintfW(statusTxt, L"%s", txt);
    statusCol = col;
    if (hMain) InvalidateRect(hMain, NULL, TRUE);
}

/* ── Завантаження списку версій з GitHub API ── */
static void fetch_versions(void) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(tmp + wcslen(tmp), L"sokil-rel.json");
    if (FAILED(URLDownloadToFileW(NULL, RELS_URL, tmp, 0, NULL))) return;
    FILE *f = _wfopen(tmp, L"rb");
    DeleteFileW(tmp);
    if (!f) return;
    long sz;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1 << 20) { fclose(f); return; }
    char *js = (char *)malloc((size_t)sz + 1);
    if (!js) { fclose(f); return; }
    fread(js, 1, (size_t)sz, f);
    js[sz] = '\0';
    fclose(f);
    ver_count = 0;
    const char *p = js;
    while (ver_count < MAX_VER) {
        const char *tag = strstr(p, "\"tag_name\":\"");
        if (!tag) break;
        tag += 12;
        const char *tag_end = strchr(tag, '"');
        if (!tag_end) break;
        /* вікно до наступного релізу */
        const char *next = strstr(tag_end, "\"tag_name\":\"");
        size_t win = next ? (size_t)(next - tag_end) : (size_t)(js + sz - tag_end);
        if (win > 8000) win = 8000;
        /* шукаємо asset sokil* */
        const char *q = tag_end;
        const char *wstop = tag_end + (win < 6000 ? win : 6000);
        const char *url = NULL;
        while (q < wstop && q[1]) {
            const char *nm = strstr(q, "\"name\":\"sokil");
            if (!nm || nm >= wstop) break;
            const char *du = strstr(nm, "\"browser_download_url\":\"");
            if (du && du < wstop) {
                const char *du_end = strchr(du + 24, '"');
                if (du_end) {
                    url = du + 24;
                    int ulen = (int)(du_end - url);
                    if (ulen > 0 && ulen < 700) {
                        int taglen = (int)(tag_end - tag);
                        if (taglen > 0 && taglen < 64) {
                            char tmpu[700], tmpt[64];
                            memcpy(tmpt, tag, taglen); tmpt[taglen] = 0;
                            memcpy(tmpu, url, ulen); tmpu[ulen] = 0;
                            MultiByteToWideChar(CP_UTF8, 0, tmpt, -1, ver_urls[ver_count], 64);
                            MultiByteToWideChar(CP_UTF8, 0, tmpu, -1, ver_urls[ver_count] + 64, 700);
                            ver_count++;
                        }
                    }
                    break;
                }
            }
            q = nm + 1;
        }
        p = (next ? next : tag_end);
    }
    free(js);
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        hMain = hw;
        fTitle = CreateFontW(26, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fBody  = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fSmall = CreateFontW(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        hbrCard = CreateSolidBrush(M3_CARD);
        hbrSurface = CreateSolidBrush(M3_SURFACE);

        CreateWindowW(L"STATIC", L"Каталог", WS_CHILD|WS_VISIBLE, 38, 100, 360, 20, hw, NULL, NULL, NULL);
        CreateWindowW(L"STATIC", L"Файли .sokil", WS_CHILD|WS_VISIBLE, 38, 126, 360, 20, hw, NULL, NULL, NULL);
        CreateWindowW(L"STATIC", L"Версія (з GitHub):", WS_CHILD|WS_VISIBLE, 38, 154, 300, 18, hw, NULL, NULL, NULL);

        hCombo = CreateWindowW(L"COMBOBOX", NULL,
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|CBS_DROPDOWNLIST,
            38, 174, 330, 220, hw, NULL, NULL, NULL);
        SendMessageW(hCombo, WM_SETFONT, (WPARAM)fBody, TRUE);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Вбудована версія (офлайн)");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Остання версія з GitHub (latest)");
        fetch_versions();
        for (int i = 0; i < ver_count; i++)
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)ver_urls[i]);
        SendMessageW(hCombo, CB_SETCURSEL, 2, 0);   /* latest за замовчуванням */

        hChkPath = CreateWindowW(L"BUTTON", L"Патчити PATH (додати до PATH)",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP,
            38, 206, 300, 26, hw, NULL, NULL, NULL);
        SendMessageW(hChkPath, WM_SETFONT, (WPARAM)fBody, TRUE);
        SendMessageW(hChkPath, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindowW(L"BUTTON", L"Встановити",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP, 24, 246, 128, 44, hw, (HMENU)BTN_INSTALL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Оновити",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP, 160, 246, 128, 44, hw, (HMENU)BTN_UPDATE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Видалити",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP, 296, 246, 120, 44, hw, (HMENU)BTN_UNINSTALL, NULL, NULL);

        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            DWORD pref = 2;
            HRESULT (WINAPI *fn)(HWND, DWORD, LPCVOID, DWORD) =
                (HRESULT (WINAPI *)(HWND,DWORD,LPCVOID,DWORD))GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (fn) fn(hw, 33, &pref, sizeof pref);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        SetBkMode(hdc, TRANSPARENT);
        RECT rc;
        HGDIOBJ old = SelectObject(hdc, fTitle);
        SetTextColor(hdc, M3_PRIMARY);
        rc = (RECT){24, 18, 400, 54};
        DrawTextW(hdc, L"Сокіл", -1, &rc, DT_LEFT|DT_SINGLELINE);
        SelectObject(hdc, fBody);
        SetTextColor(hdc, M3_MUTED);
        rc = (RECT){24, 52, 400, 74};
        DrawTextW(hdc, L"Мова програмування · Інсталятор", -1, &rc, DT_LEFT|DT_SINGLELINE);

        RECT card = {24, 86, 420, 240};
        b_round_fill(hdc, &card, M3_CARD, 12);
        SetTextColor(hdc, M3_ON_SURFACE);
        wchar_t line[1024];
        wsprintfW(line, L"%s", APP_DIR);
        rc = (RECT){150, 100, 408, 120}; DrawTextW(hdc, line, -1, &rc, DT_LEFT|DT_SINGLELINE);
        wsprintfW(line, L"%s", assoc_has() ? L"так" : L"ні");
        rc = (RECT){150, 126, 408, 146}; DrawTextW(hdc, line, -1, &rc, DT_LEFT|DT_SINGLELINE);
        SelectObject(hdc, fSmall);
        rc = (RECT){150, 156, 408, 174}; DrawTextW(hdc, L"— потрібна при виборі версії", -1, &rc, DT_LEFT|DT_SINGLELINE);

        SelectObject(hdc, fBody);
        SetTextColor(hdc, statusCol);
        rc = (RECT){24, 300, 420, 322};
        DrawTextW(hdc, statusTxt, -1, &rc, DT_LEFT|DT_SINGLELINE);

        SelectObject(hdc, fSmall);
        SetTextColor(hdc, M3_OUTLINE);
        rc = (RECT){24, 352, 420, 370};
        DrawTextW(hdc, L"Сокіл v2.4 · C99 · zero dependencies · MIT", -1, &rc, DT_LEFT|DT_SINGLELINE);
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
        if (di->CtlID == BTN_UNINSTALL) {
            bg = down ? RGB(0xDC,0xC8,0xE6) : M3_TONAL;
            fg = M3_TONAL_TXT;
        } else if (di->CtlID == BTN_UPDATE) {
            bg = down ? RGB(0xB9,0xA8,0xDA) : M3_TONAL;
            fg = M3_TONAL_TXT;
        } else {
            bg = down ? M3_PRIMARY_DARK : M3_PRIMARY;
            fg = RGB(255,255,255);
        }
        b_round_fill(di->hDC, &rc, bg, 10);
        HFONT f = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
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
        case BTN_INSTALL: {
            BOOL patch = SendMessageW(hChkPath, BM_GETCHECK, 0, 0) == BST_CHECKED;
            int ver = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (ver < 0) ver = 0;
            BOOL ok = do_install(patch, ver);
            set_status(ok ? L"✓ Встановлено! Новий термінал →  sokil файл.sokil"
                          : L"✗ Помилка встановлення чи завантаження", ok ? M3_TXT_OK : M3_TXT_ERR);
            InvalidateRect(hw, NULL, TRUE);
            break;
        }
        case BTN_UPDATE: {
            BOOL patch = SendMessageW(hChkPath, BM_GETCHECK, 0, 0) == BST_CHECKED;
            int ver = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (ver < 0) ver = 0;
            BOOL ok = do_install(patch, ver);
            set_status(ok ? L"✓ Оновлено!" : L"✗ Помилка оновлення чи завантаження", ok ? M3_TXT_OK : M3_TXT_ERR);
            InvalidateRect(hw, NULL, TRUE);
            break;
        }
        case BTN_UNINSTALL:
            do_uninstall();
            set_status(L"✓ Видалено. Можна закрити вікно.", M3_TXT_OK);
            InvalidateRect(hw, NULL, TRUE);
            break;
        }
        break;
    case WM_DESTROY:
        DeleteObject(fTitle); DeleteObject(fBody); DeleteObject(fSmall);
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
        CW_USEDEFAULT, CW_USEDEFAULT, 468, 404, NULL, NULL, hInst, NULL);
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
        else if (!wcscmp(argv[1], L"--version")) { printf("Sokil Setup v2.4\n"); return 0; }
        else if (!wcscmp(argv[1], L"--path"))    { printf("%S\n", APP_DIR); return 0; }
        else return 1;
        LocalFree(argv);
        return code;
    }
    LocalFree(argv);
    return gui_main(GetModuleHandleW(NULL));
}