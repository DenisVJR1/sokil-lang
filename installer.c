/*
 * Сокіл — власний GUI-інсталятор (100% свій, на C).
 * Вікно Setup з кнопками Встановити / Видалити.
 * Вбудовує sokil.exe, копіює у %LOCALAPPDATA%\Sokil,
 * прописує PATH та реєструє асоціацію файлів .sokil.
 *
 * Збірка: gcc -O2 -std=c99 -static -o Sokil-Setup.exe installer.c
 *         -luser32 -ladvapi32 -lshell32 -mwindows
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "sokil_exe.h"

#define BTN_INSTALL   1001
#define BTN_UNINSTALL 1002

static char APP_DIR[MAX_PATH];
static HWND hStatus;
static HBRUSH hbrBg;
static HFONT hFont;

static void init_app_dir(void) {
    GetEnvironmentVariableA("LOCALAPPDATA", APP_DIR, MAX_PATH);
    if (APP_DIR[0] == '\0')
        { GetEnvironmentVariableA("USERPROFILE", APP_DIR, MAX_PATH); lstrcatA(APP_DIR, "\\AppData\\Local"); }
    lstrcatA(APP_DIR, "\\Sokil");
}

/* ── PATH ── */
static void notify(void) {
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
}
static int get_path(char *buf, DWORD n) {
    DWORD sz = n;
    LONG r = RegGetValueA(HKEY_CURRENT_USER, "Environment", "Path",
                          RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ, NULL, buf, &sz);
    if (r != ERROR_SUCCESS) { buf[0] = '\0'; return 0; }
    buf[n-1] = '\0'; return 1;
}
static void add_to_path(const char *dir) {
    char path[4096]; get_path(path, sizeof path);
    if (strstr(path, dir)) return;
    char np[8192]; wsprintfA(np, "%s;%s", path, dir);
    RegSetKeyValueA(HKEY_CURRENT_USER, "Environment", "Path", REG_EXPAND_SZ, np, (DWORD)strlen(np)+1);
    notify();
}
static void remove_from_path(const char *dir) {
    char path[4096]; get_path(path, sizeof path);
    char *p = strstr(path, dir); if (!p) return;
    if (p > path && p[-1] == ';') p--;
    else p += strlen(dir);
    memmove(p, p + strlen(p) - strlen(p) + strlen(p + strlen(dir) + (p[-1]==';' ? 0 : 1)),
            strlen(p + strlen(dir) + 1) + 1);
    /* простий підхід: перебудувати */
    { char *s = path, *o = path; while (*s) {
        if (s == strstr(s, dir)) { s += strlen(dir); if (*s == ';') s++; }
        else { while (*s && *s != ';') *o++ = *s++; if (*s == ';') *o++ = *s++; }
    } *o = '\0'; }
    RegSetKeyValueA(HKEY_CURRENT_USER, "Environment", "Path", REG_EXPAND_SZ, path, (DWORD)strlen(path)+1);
    notify();
}

/* ── Асоціація .sokil ── */
static void assoc_add(const char *exe) {
    char cmd[512]; wsprintfA(cmd, "\"%s\" \"%%1\"", exe);
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\Classes", ".sokil", REG_SZ, "SokilScript", 0);
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\Classes\\SokilScript", NULL, REG_SZ, "Sokil Script", 0);
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\Classes\\SokilScript\\shell\\open\\command", NULL, REG_SZ, cmd, 0);
}
static void assoc_del(void) {
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\.sokil");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\SokilScript");
}

/* ── Install / Uninstall ── */
static BOOL do_install(void) {
    if (!CreateDirectoryA(APP_DIR, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    char exe[MAX_PATH]; wsprintfA(exe, "%s\\sokil.exe", APP_DIR);
    HANDLE h = CreateFileA(exe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w; WriteFile(h, sokil_exe_data, sokil_exe_len, &w, NULL); CloseHandle(h);
    if (w != sokil_exe_len) return FALSE;
    add_to_path(APP_DIR);
    assoc_add(exe);
    return TRUE;
}
static BOOL do_uninstall(void) {
    assoc_del();
    remove_from_path(APP_DIR);
    char exe[MAX_PATH]; wsprintfA(exe, "%s\\sokil.exe", APP_DIR);
    DeleteFileA(exe); RemoveDirectoryA(APP_DIR);
    return TRUE;
}

/* ── GUI ── */
static void set_status(const char *txt, COLORREF col) {
    SetWindowTextA(hStatus, txt);
    if (hbrBg) DeleteObject(hbrBg);
    hbrBg = CreateSolidBrush(col);
    InvalidateRect(hStatus, NULL, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
        HWND hTitle = CreateWindowA("STATIC",
            "  Sokil (Сокіл) — Встановлення мови програмування",
            WS_CHILD|WS_VISIBLE|SS_LEFT, 20, 20, 360, 30, hw, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)hFont, TRUE);
        char info[512];
        wsprintfA(info, "Каталог: %s\nАсоціація: .sokil\nPATH + реєстр", APP_DIR);
        HWND hInfo = CreateWindowA("STATIC", info,
            WS_CHILD|WS_VISIBLE|SS_LEFT, 20, 60, 360, 60, hw, NULL, NULL, NULL);
        SendMessage(hInfo, WM_SETFONT, (WPARAM)hFont, TRUE);
        CreateWindowA("BUTTON", "Встановити",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 20, 140, 170, 40, hw, (HMENU)BTN_INSTALL, NULL, NULL);
        CreateWindowA("BUTTON", "Видалити",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 210, 140, 170, 40, hw, (HMENU)BTN_UNINSTALL, NULL, NULL);
        hStatus = CreateWindowA("STATIC", "",
            WS_CHILD|WS_VISIBLE|SS_LEFT, 20, 200, 360, 30, hw, NULL, NULL, NULL);
        SendMessage(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == BTN_INSTALL)
            set_status(do_install() ? "Встановлено! Відкрийте новий термінал:  sokil" : "Помилка!",
                       do_install() ? 0 : 0);
        if (LOWORD(wp) == BTN_UNINSTALL) {
            do_uninstall();
            set_status("Видалено. PATH та .sokil оновлено.", RGB(0,0,180));
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == hStatus && hbrBg) { SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)hbrBg; }
        break;
    case WM_DESTROY:
        if (hFont) DeleteObject(hFont);
        if (hbrBg) DeleteObject(hbrBg);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hw, msg, wp, lp);
}

static int gui_main(void) {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof wc; wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = "SokilSetup";
    RegisterClassExA(&wc);
    HWND hw = CreateWindowExA(0, "SokilSetup", "Сокіл — Встановлення",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 410, 270, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hw, SW_SHOW);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    return (int)msg.wParam;
}

int main(int argc, char **argv) {
    init_app_dir();
    if (argc > 1) {
        if (!strcmp(argv[1], "--install"))   { do_install();  return 0; }
        if (!strcmp(argv[1], "--uninstall")) { do_uninstall(); return 0; }
        if (!strcmp(argv[1], "--version"))   { printf("Sokil Setup v2.1\n"); return 0; }
    }
    return gui_main();
}