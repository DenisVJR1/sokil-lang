/*
 * Сокіл — власний інсталятор мови (100% свій, на C).
 * Вбудовує sokil.exe у себе, копіює у %LOCALAPPDATA%\Sokil,
 * прописує PATH у реєстрі (HKCU) як Python installer.
 *
 * Збірка: gcc -O2 -std=c99 -static -o Sokil-Setup.exe installer.c
 *
 * Запуск:
 *   Sokil-Setup.exe              — інтерактивне меню
 *   Sokil-Setup.exe --install    — тиха установка
 *   Sokil-Setup.exe --uninstall  — видалення
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "sokil_exe.h"

static void app_dir(char *buf, size_t n) {
    GetEnvironmentVariableA("LOCALAPPDATA", buf, (DWORD)n);
    if (buf[0] == '\0') {          /* фолбек: %USERPROFILE%\AppData\Local */
        GetEnvironmentVariableA("USERPROFILE", buf, (DWORD)n);
        strncat(buf, "\\AppData\\Local", n - strlen(buf) - 1);
    }
    strncat(buf, "\\Sokil", n - strlen(buf) - 1);
}

static void notify_path_change(void) {
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
}

static int path_has(const char *path, const char *dir) {
    char t[4096], d[4096];
    snprintf(t, sizeof t, ";%s;", path);
    snprintf(d, sizeof d, ";%s;", dir);
    return strstr(t, d) != NULL;
}

static int get_user_path(char *buf, size_t n) {
    DWORD sz = (DWORD)n;
    LONG r = RegGetValueA(HKEY_CURRENT_USER, "Environment", "Path",
                          RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, buf, &sz);
    if (r != ERROR_SUCCESS || sz == 0) { buf[0] = '\0'; return 0; }
    buf[n - 1] = '\0';
    return 1;
}

static int add_to_path(const char *dir) {
    char path[4096];
    get_user_path(path, sizeof path);
    if (path_has(path, dir)) return 1;              /* уже є */
    char newpath[8192];
    snprintf(newpath, sizeof newpath, "%s;%s", path, dir);
    LONG r = RegSetKeyValueA(HKEY_CURRENT_USER, "Environment", "Path",
                             REG_EXPAND_SZ, newpath, (DWORD)strlen(newpath) + 1);
    if (r != ERROR_SUCCESS) {
        fprintf(stderr, "Помилка запису PATH (код %ld)\n", r);
        return 0;
    }
    notify_path_change();
    return 1;
}

static int remove_from_path(const char *dir) {
    char path[4096], out[8192];
    get_user_path(path, sizeof path);
    /* вирізаємо ;dir та dir; */
    char *p = strstr(path, dir);
    if (!p) return 1;
    out[0] = '\0';
    size_t o = 0;
    const char *s = path;
    while (s && *s) {
        const char *f = strstr(s, dir);
        if (!f || (f != s && f[-1] != ';')) {       /* не наш шлях */
            size_t piece = f ? (size_t)(f - s) : strlen(s);
            memcpy(out + o, s, piece); o += piece;
            s = f ? f : s + piece;
            continue;
        }
        /* пропускаємо dir та навколишні ';' */
        const char *e = f + strlen(dir);
        if (e > s && s != f && f[-1] == ';' && (*e == ';' || *e == '\0')) {
            o -= (size_t)(o > 0 && out[o-1] == ';');
        }
        if (*e == ';') e++;
        s = e;
    }
    out[o] = '\0';
    LONG r = RegSetKeyValueA(HKEY_CURRENT_USER, "Environment", "Path",
                             REG_EXPAND_SZ, out, (DWORD)strlen(out) + 1);
    if (r != ERROR_SUCCESS) return 0;
    notify_path_change();
    return 1;
}

static int install(void) {
    char dir[MAX_PATH];
    app_dir(dir, sizeof dir);
    if (!CreateDirectoryA(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Не вдалося створити %s\n", dir);
        return 0;
    }
    char exe[MAX_PATH];
    snprintf(exe, sizeof exe, "%s\\sokil.exe", dir);
    HANDLE h = CreateFileA(exe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Не вдалося записати %s\n", exe);
        return 0;
    }
    DWORD wrote = 0;
    WriteFile(h, sokil_exe_data, sokil_exe_len, &wrote, NULL);
    CloseHandle(h);
    if (wrote != sokil_exe_len) {
        fprintf(stderr, "Запис неповний\n");
        return 0;
    }
    if (!add_to_path(dir)) return 0;
    printf("Встановлено!\n");
    printf("  Мова:      %s\n", exe);
    printf("  PATH:      додано (відкрий новий термінал)\n");
    printf("  Запуск:    sokil або sokil файл.sokil\n");
    printf("  Видалення: Sokil-Setup.exe --uninstall\n");
    return 1;
}

static int uninstall(void) {
    char dir[MAX_PATH];
    app_dir(dir, sizeof dir);
    remove_from_path(dir);
    char exe[MAX_PATH];
    snprintf(exe, sizeof exe, "%s\\sokil.exe", dir);
    DeleteFileA(exe);
    RemoveDirectoryA(dir);
    printf("Видалено. PATH оновлено (відкрий новий термінал).\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "--install"))   return install() ? 0 : 1;
        if (!strcmp(argv[1], "--uninstall")) return uninstall() ? 0 : 1;
        if (!strcmp(argv[1], "--version")) {
            printf("Сокіл інсталятор v1.0\n");
            return 0;
        }
    }
    printf("=== Сокіл — інсталятор мови ===\n");
    printf("1) Встановити\n");
    printf("2) Видалити\n");
    printf("3) Вийти\n");
    printf("Вибір: ");
    fflush(stdout);
    char c = getchar();
    if (c == '1') return install() ? 0 : 1;
    if (c == '2') return uninstall() ? 0 : 1;
    return 0;
}