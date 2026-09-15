/*
 * Сокіл — інсталятор-майстер (wizard) у стилі Inno Setup, дизайн Material 3.
 * 100% власний код на C (Win32 API), без залежностей.
 *
 * v2.5: сторінки майстра (Вітання → Шлях → Готово), анімований фон з кодом,
 *       вибір версії з GitHub, асоціація .sokil через cmd /k (подвійний клік),
 *       фон Material 3 замість системного.
 */
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <urlmon.h>
#include <stdio.h>
#include <string.h>

#include "sokil_exe.h"

#define BTN_NEXT    1001
#define BTN_BACK    1002
#define BTN_CLOSE   1003
#define BTN_INSTALL 1004
#define BTN_UPDATE  1005
#define BTN_UNINST  1006
#define BTN_BROWSE  1007

#define M3_PRIMARY      0x00A45067   /* 6750A4 */
#define M3_PRIMARY_DARK 0x0058358B
#define M3_SURFACE      0x00F7FEFE
#define M3_CARD         0x00EDF7F3
#define M3_TONAL        0x00FFDDEA
#define M3_TONAL_TXT    0x004C2100
#define M3_ON_SURFACE   0x001D1B20
#define M3_MUTED        0x00797875
#define M3_OUTLINE      0x00797875

static wchar_t APP_DIR[MAX_PATH];
static HWND hMain, hChkPath, hCombo, hPath;
static HFONT fTitle, fBody, fSmall, fMono;
static HBRUSH hbrCard, hbrSurface;
static wchar_t statusTxt[512];
static int g_page = 0;
static int g_anim = 0;

#define MAX_VER 16
static wchar_t ver_urls[MAX_VER][768];
static int ver_count = 0;

#define RELS_URL L"https://api.github.com/repos/DenisVJR1/sokil-lang/releases?per_page=12"
#define LATEST   L"https://github.com/DenisVJR1/sokil-lang/releases/latest/download/sokil.exe"

/* ── Фоновий «білий код» — рядки прокручуються ліворуч ── */
static const wchar_t *BG_CODE[] = {
    L"print('Сокіл')", L"let a = [1,2,3]; sort(a)", L"fn hello()",
    L"for i in range(5) { print(i) }", L"sokil --compile app.sokil",
    L"x = 42", L"random(1, 100)", L"print('привіт, світе!')",
    L"while x > 0 { x = x - 1 }", L"contains('sokil','ok')",
    L"input('як тебе звати?')", L"sleep(250)", L"join(['a','b'], '-')",
};
#define BG_N (int)(sizeof BG_CODE / sizeof BG_CODE[0])

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

/* ── Асоціація .sokil: подвійний клік → запуск через cmd /k ── */
static void assoc_add(const wchar_t *exe) {
    wchar_t cmd[1024];
    wsprintfW(cmd, L"cmd.exe /k \"\"%s\" \"%%1\"\"", exe);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil\\DefaultIcon", NULL, REG_SZ, exe, (DWORD)((wcslen(exe)+1)*sizeof(wchar_t)));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil", NULL, REG_SZ, L"SokilScript", 12 * sizeof(wchar_t));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript\\shell\\open\\command", NULL, REG_SZ, cmd, (DWORD)((wcslen(cmd)+1)*sizeof(wchar_t)));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript\\DefaultIcon", NULL, REG_SZ, exe, (DWORD)((wcslen(exe)+1)*sizeof(wchar_t)));
}
static void assoc_del(void) {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.sokil");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SokilScript");
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
static void set_status(const wchar_t *txt) {
    wsprintfW(statusTxt, L"%s", txt);
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
                    int taglen = (int)(tag_end - tag);
                    int ulen = (int)(du_end - (du + 24));
                    if (taglen > 0 && taglen < 64 && ulen > 0 && ulen < 700) {
                        char tmpt[64], tmpu[700];
                        memcpy(tmpt, tag, taglen); tmpt[taglen] = 0;
                        memcpy(tmpu, du + 24, ulen); tmpu[ulen] = 0;
                        MultiByteToWideChar(CP_UTF8, 0, tmpt, -1, ver_urls[ver_count], 64);
                        MultiByteToWideChar(CP_UTF8, 0, tmpu, -1, ver_urls[ver_count] + 64, 700);
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
    for (int i = 0; i < 3; i++) {
        if (ctrls[i]) ShowWindow(ctrls[i], (page == 1) ? SW_SHOW : SW_HIDE);
    }
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
        fTitle = CreateFontW(26, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fBody  = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fSmall = CreateFontW(9, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        fMono  = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");
        hbrCard = CreateSolidBrush(M3_CARD);
        hbrSurface = CreateSolidBrush(M3_SURFACE);

        CreateWindowW(L"STATIC", L"Каталог встановлення:", WS_CHILD, 38, 98, 340, 20, hw, NULL, NULL, NULL);
        hPath = CreateWindowW(L"EDIT", APP_DIR, WS_CHILD|WS_BORDER|ES_AUTOHSCROLL, 38, 120, 322, 26, hw, NULL, NULL, NULL);
        SendMessageW(hPath, WM_SETFONT, (WPARAM)fBody, TRUE);
        CreateWindowW(L"BUTTON", L"Огляд...", WS_CHILD|BS_PUSHBUTTON, 368, 120, 82, 26, hw, (HMENU)BTN_BROWSE, NULL, NULL);
        hChkPath = CreateWindowW(L"BUTTON", L"Патчити PATH (запуск з будь-якого терміналу)",
            WS_CHILD|BS_AUTOCHECKBOX|WS_TABSTOP, 38, 160, 380, 24, hw, NULL, NULL, NULL);
        SendMessageW(hChkPath, WM_SETFONT, (WPARAM)fBody, TRUE);
        SendMessageW(hChkPath, BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowW(L"STATIC", L"Версія (з GitHub):", WS_CHILD, 38, 192, 340, 18, hw, NULL, NULL, NULL);
        hCombo = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST, 38, 212, 330, 220, hw, NULL, NULL, NULL);
        SendMessageW(hCombo, WM_SETFONT, (WPARAM)fBody, TRUE);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Вбудована версія (офлайн)");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Остання версія з GitHub (latest)");
        fetch_versions();
        for (int i = 0; i < ver_count; i++)
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)ver_urls[i]);
        SendMessageW(hCombo, CB_SETCURSEL, 2, 0);

        CreateWindowW(L"BUTTON", L"Далі →",   WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 24, 330, 128, 44, hw, (HMENU)BTN_NEXT, NULL, NULL);
        CreateWindowW(L"BUTTON", L"← Назад",  WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 24, 330, 128, 44, hw, (HMENU)BTN_BACK, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Закрити",  WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 160, 330, 128, 44, hw, (HMENU)BTN_CLOSE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Встановити", WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 160, 330, 128, 44, hw, (HMENU)BTN_INSTALL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Оновити",  WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 24, 330, 128, 44, hw, (HMENU)BTN_UPDATE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Видалити", WS_CHILD|BS_OWNERDRAW|WS_TABSTOP, 296, 330, 120, 44, hw, (HMENU)BTN_UNINST, NULL, NULL);

        SetTimer(hw, 1, 130, NULL);

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

        /* анімований фон-код */
        SelectObject(hdc, fMono);
        SetTextColor(hdc, 0x00F3EAFB);
        for (int i = 0; i < BG_N; i++) {
            int speed = 16 + (i % 3) * 9;
            int x = 300 - (g_anim * speed) % 640;
            int y = 78 + i * 20;
            if (x > -140 && x < 486) {
                RECT lr = {x, y, x + 300, y + 18};
                DrawTextW(hdc, BG_CODE[i], -1, &lr, DT_LEFT|DT_SINGLELINE);
            }
        }

        HGDIOBJ old = SelectObject(hdc, fTitle);
        SetTextColor(hdc, M3_PRIMARY);
        rc = (RECT){24, 16, 400, 50};
        DrawTextW(hdc, L"Сокіл", -1, &rc, DT_LEFT|DT_SINGLELINE);
        SelectObject(hdc, fBody);
        SetTextColor(hdc, M3_MUTED);
        rc = (RECT){24, 50, 440, 70};
        DrawTextW(hdc, L"Мова програмування · Інсталятор v2.5", -1, &rc, DT_LEFT|DT_SINGLELINE);

        if (g_page == 0) {
            SetTextColor(hdc, M3_ON_SURFACE);
            rc = (RECT){38, 96, 446, 315};
            DrawTextW(hdc,
                L"Вітаємо! Сокіл — власна мова програмування.\n\n"
                L"• Пишеш код зрозумілою мовою\n"
                L"• sokil --compile дає .exe БЕЗ компілятора C\n"
                L"• Подвійний клік по .sokil запускає програму\n"
                L"• Галочка PATH — запуск з будь-якого терміналу\n\n"
                L"Натисни «Далі», щоб обрати каталог і версію.",
                -1, &rc, DT_LEFT|DT_WORDBREAK);
        } else if (g_page == 1) {
            RECT card = {24, 86, 456, 270};
            b_round_fill(hdc, &card, M3_CARD, 12);
        } else {
            SetTextColor(hdc, M3_ON_SURFACE);
            rc = (RECT){38, 96, 446, 320};
            DrawTextW(hdc, statusTxt[0] ? statusTxt : L"Готово", -1, &rc, DT_LEFT|DT_WORDBREAK);
        }

        SelectObject(hdc, fSmall);
        SetTextColor(hdc, M3_OUTLINE);
        rc = (RECT){24, 398, 440, 414};
        DrawTextW(hdc, L"Сокіл v2.5 · C99 · zero dependencies · MIT", -1, &rc, DT_LEFT|DT_SINGLELINE);
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
                wsprintfW(msg, L"✓ Встановлено!\n\nКаталог: %s\n\n"
                    L"Запуск з терміналу:  sokil файл.sokil\n"
                    L"Компіляція у .exe:   sokil --compile файл.sokil\n"
                    L"Оновлення:           sokil --update\n\n"
                    L"Подвійний клік по .sokil відкриває програму.",
                    APP_DIR);
                set_status(msg);
            } else {
                set_status(L"✗ Помилка встановлення чи завантаження.\nПеревір інтернет або обери вбудовану версію.");
            }
            set_page(2);
            break;
        }
        case BTN_UNINST: {
            if (do_uninstall())
                set_status(L"✓ Видалено.\n\nСокіл прибрано: файли, PATH, асоціація .sokil.");
            else
                set_status(L"✗ Помилка видалення.");
            set_page(2);
            break;
        }
        }
        break;
    case WM_DESTROY:
        KillTimer(hw, 1);
        DeleteObject(fTitle); DeleteObject(fBody); DeleteObject(fSmall); DeleteObject(fMono);
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
        CW_USEDEFAULT, CW_USEDEFAULT, 486, 440, NULL, NULL, hInst, NULL);
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
        else if (!wcscmp(argv[1], L"--version")) { printf("Sokil Setup v2.5\n"); return 0; }
        else if (!wcscmp(argv[1], L"--path"))    { printf("%S\n", APP_DIR); return 0; }
        else return 1;
        LocalFree(argv);
        return code;
    }
    LocalFree(argv);
    return gui_main(GetModuleHandleW(NULL));
}