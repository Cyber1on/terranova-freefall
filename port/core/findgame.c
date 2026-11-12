/* findgame.c: look for an installed Terra Nova (Windows: Steam and GOG).
 *
 * Used at the first start, before the folder window asks.  Only reads: the registry for where
 * Steam and GOG keep their games, Steam's list of library folders, and then the folders
 * themselves.  Nothing is written anywhere.
 *
 *   Steam  HKCU\Software\Valve\Steam "SteamPath" (or HKLM ... "InstallPath"), then every
 *          "path" in <steam>\steamapps\libraryfolders.vdf (and config\libraryfolders.vdf of
 *          older Steam versions); in each library steamapps\appmanifest_368600.acf names the
 *          game's folder ("installdir") under steamapps\common.
 *   GOG    HKLM\SOFTWARE\GOG.com\Games\<number> "path", for every game GOG installed.
 *
 * A folder counts when it, or its TNOVA folder (Steam) or TN folder (GOG), holds __FF.EXE.
 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "platform.h"

#ifdef _WIN32
#include <windows.h>

#define STEAM_APP "368600"
#define STEAM_DIR "Terra Nova Strike Force Centauri"

static int exe_in(const char *dir)
{
    char p[1200];
    DWORD a;
    snprintf(p, sizeof p, "%s\\__FF.EXE", dir);
    a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* dir, dir\TNOVA or dir\TN holds the game: out = that folder */
static int try_dir(const char *dir, char *out, size_t n)
{
    static const char *const names[] = { "TNOVA", "TN" };     /* Steam's name, GOG's */
    char sub[1100];
    int i;
    if (!dir[0])
        return 0;
    for (i = 0; i < 2; i++) {
        snprintf(sub, sizeof sub, "%s\\%s", dir, names[i]);
        if (exe_in(sub)) {
            snprintf(out, n, "%s", sub);
            return 1;
        }
    }
    if (exe_in(dir)) {
        snprintf(out, n, "%s", dir);
        return 1;
    }
    return 0;
}

static int reg_str(HKEY root, const char *key, const char *name, REGSAM view, char *out, DWORD n)
{
    HKEY k;
    DWORD type = 0, len = n - 1;
    LONG r;
    out[0] = 0;
    if (RegOpenKeyExA(root, key, 0, KEY_QUERY_VALUE | view, &k) != ERROR_SUCCESS)
        return 0;
    r = RegQueryValueExA(k, name, NULL, &type, (BYTE *)out, &len);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        out[0] = 0;
        return 0;
    }
    out[len < n ? len : n - 1] = 0;
    return out[0] != 0;
}

/* the text between the quotes of the value in a line  "key"  "value", with \\ made \ */
static int vdf_value(const char *line, const char *key, char *out, size_t n)
{
    const char *p = line;
    size_t kl = strlen(key), i = 0;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '"' || _strnicmp(p + 1, key, kl) || p[1 + kl] != '"')
        return 0;
    p = strchr(p + 2 + kl, '"');
    if (!p)
        return 0;
    for (p++; *p && *p != '"' && i + 1 < n; p++) {
        if (*p == '\\' && p[1] == '\\')
            p++;
        out[i++] = *p;
    }
    out[i] = 0;
    return i > 0;
}

/* one Steam library folder */
static int try_steam_lib(const char *lib, char *out, size_t n)
{
    char p[1100], line[1024], name[300];
    FILE *f;
    snprintf(p, sizeof p, "%s\\steamapps\\appmanifest_" STEAM_APP ".acf", lib);
    f = fopen(p, "r");
    if (f) {                        /* the folder Steam itself says the game is in */
        int got = 0;
        while (!got && fgets(line, sizeof line, f))
            got = vdf_value(line, "installdir", name, sizeof name);
        fclose(f);
        if (got) {
            snprintf(p, sizeof p, "%s\\steamapps\\common\\%s", lib, name);
            if (try_dir(p, out, n))
                return 1;
        }
    }
    snprintf(p, sizeof p, "%s\\steamapps\\common\\" STEAM_DIR, lib);
    return try_dir(p, out, n);
}

static int try_steam(const char *steam, char *out, size_t n)
{
    static const char *const lists[] = { "steamapps\\libraryfolders.vdf", "config\\libraryfolders.vdf" };
    char p[1100], line[1024], lib[600];
    int i;
    if (!steam[0])
        return 0;
    if (try_steam_lib(steam, out, n))
        return 1;
    for (i = 0; i < 2; i++) {
        FILE *f;
        snprintf(p, sizeof p, "%s\\%s", steam, lists[i]);
        f = fopen(p, "r");
        if (!f)
            continue;
        while (fgets(line, sizeof line, f))
            if (vdf_value(line, "path", lib, sizeof lib) && try_steam_lib(lib, out, n)) {
                fclose(f);
                return 1;
            }
        fclose(f);
    }
    return 0;
}

static int try_gog(REGSAM view, char *out, size_t n)
{
    HKEY k;
    DWORD i;
    char id[128], key[300], path[600];
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games", 0,
                      KEY_ENUMERATE_SUB_KEYS | view, &k) != ERROR_SUCCESS)
        return 0;
    for (i = 0;; i++) {
        DWORD len = sizeof id;
        if (RegEnumKeyExA(k, i, id, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        snprintf(key, sizeof key, "SOFTWARE\\GOG.com\\Games\\%s", id);
        if (reg_str(HKEY_LOCAL_MACHINE, key, "path", view, path, sizeof path) && try_dir(path, out, n)) {
            RegCloseKey(k);
            return 1;
        }
    }
    RegCloseKey(k);
    return 0;
}

int port_find_game(char *out, size_t n, const char **where)
{
    static const REGSAM views[] = { KEY_WOW64_32KEY, KEY_WOW64_64KEY };
    char steam[600];
    int v;
    *where = "Steam";
    if (reg_str(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", 0, steam, sizeof steam)) {
        char *c;
        for (c = steam; *c; c++)    /* Steam writes it with forward slashes */
            if (*c == '/')
                *c = '\\';
        if (try_steam(steam, out, n))
            return 1;
    }
    for (v = 0; v < 2; v++)
        if (reg_str(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath", views[v], steam, sizeof steam) &&
            try_steam(steam, out, n))
            return 1;
    *where = "GOG";
    for (v = 0; v < 2; v++)
        if (try_gog(views[v], out, n))
            return 1;
    *where = "";
    return 0;
}

#else

int port_find_game(char *out, size_t n, const char **where)
{
    (void)out; (void)n;
    *where = "";
    return 0;
}

#endif
