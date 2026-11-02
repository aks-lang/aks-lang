/*
 * The AKS Programming Language GUI Installer
 * World-Class Minimalist, Professional Setup Wizard
 * Copyright (c) 2026 AKS Language Authors. All Rights Reserved.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <direct.h>
#include <io.h>

#include "installer_logo_bytes.h"
#include "installer_full_logo_bytes.h"

static Gdiplus::Bitmap *LoadPngFromMemory(const uint8_t *data, size_t size) {
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return NULL;
    void *pMem = GlobalLock(hMem);
    if (!pMem) {
        GlobalFree(hMem);
        return NULL;
    }
    memcpy(pMem, data, size);
    GlobalUnlock(hMem);
    IStream *pStream = NULL;
    Gdiplus::Bitmap *bmp = NULL;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == S_OK) {
        bmp = Gdiplus::Bitmap::FromStream(pStream);
        pStream->Release();
    }
    return bmp;
}

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdiplus.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "version.h"
#define AKS_VERSION AKS_VERSION_STRING
#define AKS_MAGIC "AKSPKG10"
#define DEFAULT_APP_NAME "aks"

// Window dimensions
#define WIZARD_WIDTH  580
#define WIZARD_HEIGHT 440

#define IDC_EDIT_PATH 1005

typedef enum {
    PAGE_OPTIONS,
    PAGE_MAINTENANCE,
    PAGE_INSTALLING,
    PAGE_COMPLETED,
    PAGE_UNINSTALL,
    PAGE_UNINSTALLING,
    PAGE_UNINSTALLED
} WizardPage;

#pragma pack(push, 1)
typedef struct {
    char rel_path[260];
    uint64_t file_offset;
    uint64_t file_size;
} AksPkgFileEntry;

typedef struct {
    uint32_t file_count;
    uint64_t toc_offset;
    char magic[8];
} AksPkgTrailer;
#pragma pack(pop)

// Dynamic Theme Palette
struct AppTheme {
    Gdiplus::Color bg;
    Gdiplus::Color border;
    Gdiplus::Color divider;
    Gdiplus::Color textTitle;
    Gdiplus::Color textHeading;
    Gdiplus::Color textBody;
    Gdiplus::Color textMuted;
    Gdiplus::Color cardBg;
    Gdiplus::Color cardBorder;

    Gdiplus::Color inputBg;
    Gdiplus::Color inputBorder;
    COLORREF editBg;
    COLORREF editText;

    Gdiplus::Color btnPrimaryBg;
    Gdiplus::Color btnPrimaryHover;
    Gdiplus::Color btnPrimaryPressed;
    Gdiplus::Color btnPrimaryText;

    Gdiplus::Color btnSecondaryBg;
    Gdiplus::Color btnSecondaryHover;
    Gdiplus::Color btnSecondaryBorder;
    Gdiplus::Color btnSecondaryText;

    Gdiplus::Color checkBgChecked;
    Gdiplus::Color checkBgUnchecked;
    Gdiplus::Color checkBorder;
    Gdiplus::Color checkMark;

    Gdiplus::Color progTrack;
    Gdiplus::Color progFill;
};

// Global state
static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hEditPath = NULL;
static HBRUSH g_hEditBrush = NULL;
static HFONT g_hFontNormal = NULL;
static Gdiplus::Bitmap *g_pLogoBitmap = NULL;
static Gdiplus::Bitmap *g_pLogoFullDarkBitmap = NULL;
static Gdiplus::Bitmap *g_pLogoFullLightBitmap = NULL;

static Gdiplus::Font *g_pFontTitle = NULL;
static Gdiplus::Font *g_pFontHeading = NULL;
static Gdiplus::Font *g_pFontBody = NULL;
static Gdiplus::Font *g_pFontBodyBold = NULL;
static Gdiplus::Font *g_pFontSub = NULL;
static Gdiplus::Font *g_pFontMono = NULL;
static Gdiplus::Font *g_pFontBtn = NULL;
static Gdiplus::Font *g_pFontChevron = NULL;

static WizardPage g_currentPage = PAGE_OPTIONS;
static char g_targetDir[MAX_PATH] = {0};
static char g_installedDir[MAX_PATH] = {0};
static char g_selfPath[MAX_PATH] = {0};
static bool g_optPath = true;
static bool g_optAssoc = true;
static bool g_optVSCode = true;
static bool g_optOpenTerm = true;
static bool g_isUninstallMode = false;
static bool g_isAlreadyInstalled = false;
static bool g_isRepairMode = false;
static bool g_isSilent = false;
static bool g_isDarkMode = false;

// Progress tracking
static int g_installProgressPct = 0;
static char g_installCurrentFile[MAX_PATH] = "Initializing...";
static char g_installStepText[128] = "Preparing toolchain components...";

// Symmetrical 40px left and right margins across all controls (40 to 540)
static const RECT RC_BTN_BROWSE     = {454, 134, 540, 168}; // 86x34
static const RECT RC_ROW_PATH       = {40, 206, 540, 246};  // Option 1
static const RECT RC_ROW_ASSOC      = {40, 252, 540, 292};  // Option 2
static const RECT RC_ROW_VSCODE     = {40, 298, 540, 338};  // Option 3
static const RECT RC_ROW_TERM       = {40, 268, 540, 308};  // Terminal toggle
static const RECT RC_CARD_REPAIR    = {40, 156, 540, 230};  // Maintenance Repair Card (500x74)
static const RECT RC_CARD_UNINSTALL = {40, 244, 540, 318};  // Maintenance Uninstall Card (500x74)
static const RECT RC_BTN_CLOSE      = {436, 380, 540, 418}; // Maintenance Close Button (104x38)
static const RECT RC_BTN_CANCEL     = {334, 380, 426, 418}; // 92x38
static const RECT RC_BTN_INSTALL    = {436, 380, 540, 418}; // 104x38
static const RECT RC_BTN_FINISH     = {436, 380, 540, 418}; // 104x38

// Hover & Press states
static bool g_hoverBrowse = false;
static bool g_hoverRowPath = false;
static bool g_hoverRowAssoc = false;
static bool g_hoverRowVSCode = false;
static bool g_hoverRowTerm = false;
static bool g_hoverRepair = false;
static bool g_hoverUninstall = false;
static bool g_hoverClose = false;
static bool g_hoverCancel = false;
static bool g_hoverInstall = false;
static bool g_hoverFinish = false;

static bool g_pressBrowse = false;
static bool g_pressRepair = false;
static bool g_pressUninstall = false;
static bool g_pressClose = false;
static bool g_pressCancel = false;
static bool g_pressInstall = false;
static bool g_pressFinish = false;

// Per-Monitor DPI Awareness & Dynamic Scaling Support
typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
typedef UINT (WINAPI *GetDpiForSystemFn)(void);

static float g_dpiScale = 1.0f;

static void InitDpiAwareness(void) {
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContextFn pSetDpi = 
            (SetProcessDpiAwarenessContextFn)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetDpi) {
            pSetDpi((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            return;
        }
    }
    HMODULE hShcore = LoadLibraryA("Shcore.dll");
    if (hShcore) {
        typedef HRESULT (WINAPI *SetProcessDpiAwarenessFn)(int);
        SetProcessDpiAwarenessFn pSetAware = 
            (SetProcessDpiAwarenessFn)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (pSetAware) pSetAware(2); // PROCESS_PER_MONITOR_DPI_AWARE
        FreeLibrary(hShcore);
        return;
    }
    SetProcessDPIAware();
}

static float DetectCurrentDpiScale(HWND hWnd) {
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        if (hWnd) {
            GetDpiForWindowFn pGetDpiForWindow = 
                (GetDpiForWindowFn)GetProcAddress(hUser32, "GetDpiForWindow");
            if (pGetDpiForWindow) {
                UINT dpi = pGetDpiForWindow(hWnd);
                if (dpi > 0) return (float)dpi / 96.0f;
            }
        }
        GetDpiForSystemFn pGetDpiForSystem = 
            (GetDpiForSystemFn)GetProcAddress(hUser32, "GetDpiForSystem");
        if (pGetDpiForSystem) {
            UINT dpi = pGetDpiForSystem();
            if (dpi > 0) return (float)dpi / 96.0f;
        }
    }
    HDC hdc = GetDC(NULL);
    if (hdc) {
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        if (dpiX > 0) return (float)dpiX / 96.0f;
    }
    return 1.0f;
}

// Theme detection
static bool DetectIsDarkMode(void) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1;
        DWORD size = sizeof(val);
        DWORD type = REG_DWORD;
        if (RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val == 0); // 0 = dark mode, 1 = light mode
        }
        RegCloseKey(hKey);
    }
    return false;
}

static void ApplyImmersiveDarkMode(HWND hWnd, bool isDark) {
    HMODULE hDwm = LoadLibraryA("dwmapi.dll");
    if (hDwm) {
        typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
        DwmSetWindowAttributeFn pfn = (DwmSetWindowAttributeFn)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (pfn) {
            BOOL dark = isDark ? TRUE : FALSE;
            pfn(hWnd, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
            pfn(hWnd, 19, &dark, sizeof(dark)); // Older Windows 10
        }
        FreeLibrary(hDwm);
    }
}

static AppTheme GetCurrentTheme(bool isDark) {
    AppTheme t;
    if (isDark) {
        t.bg                 = Gdiplus::Color(255, 24, 24, 27);     // Zinc 900
        t.border             = Gdiplus::Color(255, 39, 39, 42);     // Zinc 800
        t.divider            = Gdiplus::Color(255, 39, 39, 42);
        t.textTitle          = Gdiplus::Color(255, 255, 255, 255);  // Pure Crisp White
        t.textHeading        = Gdiplus::Color(255, 255, 255, 255);  // Pure Crisp White
        t.textBody           = Gdiplus::Color(255, 226, 232, 240);  // Slate 200
        t.textMuted          = Gdiplus::Color(255, 178, 186, 198);  // Slate 350 - high contrast, razor-sharp
        t.cardBg             = Gdiplus::Color(220, 30, 30, 34);
        t.cardBorder         = Gdiplus::Color(255, 48, 48, 54);

        t.inputBg            = Gdiplus::Color(255, 32, 32, 36);
        t.inputBorder        = Gdiplus::Color(255, 63, 63, 70);     // Zinc 700
        t.editBg             = RGB(32, 32, 36);
        t.editText           = RGB(250, 250, 250);

        t.btnPrimaryBg       = Gdiplus::Color(255, 14, 165, 233);   // Sky 500
        t.btnPrimaryHover    = Gdiplus::Color(255, 56, 189, 248);   // Sky 400
        t.btnPrimaryPressed  = Gdiplus::Color(255, 2, 132, 199);    // Sky 600
        t.btnPrimaryText     = Gdiplus::Color(255, 255, 255, 255);

        t.btnSecondaryBg     = Gdiplus::Color(255, 36, 36, 40);
        t.btnSecondaryHover  = Gdiplus::Color(255, 52, 52, 58);
        t.btnSecondaryBorder = Gdiplus::Color(255, 68, 68, 76);
        t.btnSecondaryText   = Gdiplus::Color(255, 244, 244, 245);

        t.checkBgChecked     = Gdiplus::Color(255, 14, 165, 233);
        t.checkBgUnchecked   = Gdiplus::Color(255, 32, 32, 36);
        t.checkBorder        = Gdiplus::Color(255, 82, 82, 91);
        t.checkMark          = Gdiplus::Color(255, 255, 255, 255);

        t.progTrack          = Gdiplus::Color(255, 39, 39, 42);
        t.progFill           = Gdiplus::Color(255, 14, 165, 233);
    } else {
        t.bg                 = Gdiplus::Color(255, 255, 255, 255);  // Pure White
        t.border             = Gdiplus::Color(255, 226, 232, 240);  // Slate 200
        t.divider            = Gdiplus::Color(255, 226, 232, 240);
        t.textTitle          = Gdiplus::Color(255, 15, 23, 42);     // Slate 900
        t.textHeading        = Gdiplus::Color(255, 30, 41, 59);     // Slate 800
        t.textBody           = Gdiplus::Color(255, 51, 65, 85);     // Slate 700
        t.textMuted          = Gdiplus::Color(255, 100, 116, 139);  // Slate 500
        t.cardBg             = Gdiplus::Color(230, 248, 250, 252);
        t.cardBorder         = Gdiplus::Color(255, 226, 232, 240);  // Slate 200

        t.inputBg            = Gdiplus::Color(255, 255, 255, 255);
        t.inputBorder        = Gdiplus::Color(255, 203, 213, 225);  // Slate 300
        t.editBg             = RGB(255, 255, 255);
        t.editText           = RGB(15, 23, 42);

        t.btnPrimaryBg       = Gdiplus::Color(255, 2, 132, 199);    // Sky 600
        t.btnPrimaryHover    = Gdiplus::Color(255, 14, 165, 233);   // Sky 500
        t.btnPrimaryPressed  = Gdiplus::Color(255, 3, 105, 161);    // Sky 700
        t.btnPrimaryText     = Gdiplus::Color(255, 255, 255, 255);

        t.btnSecondaryBg     = Gdiplus::Color(255, 255, 255, 255);
        t.btnSecondaryHover  = Gdiplus::Color(255, 241, 245, 249);  // Slate 100
        t.btnSecondaryBorder = Gdiplus::Color(255, 203, 213, 225);  // Slate 300
        t.btnSecondaryText   = Gdiplus::Color(255, 51, 65, 85);

        t.checkBgChecked     = Gdiplus::Color(255, 2, 132, 199);
        t.checkBgUnchecked   = Gdiplus::Color(255, 255, 255, 255);
        t.checkBorder        = Gdiplus::Color(255, 203, 213, 225);
        t.checkMark          = Gdiplus::Color(255, 255, 255, 255);

        t.progTrack          = Gdiplus::Color(255, 241, 245, 249);
        t.progFill           = Gdiplus::Color(255, 2, 132, 199);
    }
    return t;
}

static void GetDefaultInstallDir(char *out, size_t max_len) {
    char local_app_data[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, sizeof(local_app_data)) > 0) {
        snprintf(out, max_len, "%s\\Programs\\%s", local_app_data, DEFAULT_APP_NAME);
        return;
    }
    char user_profile[MAX_PATH];
    if (GetEnvironmentVariableA("USERPROFILE", user_profile, sizeof(user_profile)) > 0) {
        snprintf(out, max_len, "%s\\.aks", user_profile);
        return;
    }
    snprintf(out, max_len, "C:\\aks");
}

static bool CheckExistingInstallation(char *out_dir, size_t max_len) {
    // 1. Check Windows Installed Apps registry entry
    HKEY hKey;
    const char *key_path = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AKSLang";
    if (RegOpenKeyExA(HKEY_CURRENT_USER, key_path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char loc[MAX_PATH] = {0};
        DWORD loc_len = sizeof(loc);
        DWORD type = REG_SZ;
        if (RegQueryValueExA(hKey, "InstallLocation", NULL, &type, (LPBYTE)loc, &loc_len) == ERROR_SUCCESS && strlen(loc) > 0) {
            char test_exe[MAX_PATH];
            snprintf(test_exe, sizeof(test_exe), "%s\\bin\\aks.exe", loc);
            if (_access(test_exe, 0) == 0) {
                snprintf(out_dir, max_len, "%s", loc);
                RegCloseKey(hKey);
                return true;
            }
        }
        RegCloseKey(hKey);
    }

    // 2. Check standard %LOCALAPPDATA%\Programs\aks directory
    char local_app_data[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, sizeof(local_app_data)) > 0) {
        char def_dir[MAX_PATH];
        snprintf(def_dir, sizeof(def_dir), "%s\\Programs\\%s", local_app_data, DEFAULT_APP_NAME);
        char test_exe[MAX_PATH];
        snprintf(test_exe, sizeof(test_exe), "%s\\bin\\aks.exe", def_dir);
        if (_access(test_exe, 0) == 0) {
            snprintf(out_dir, max_len, "%s", def_dir);
            return true;
        }
    }

    // 3. Check AKS_HOME environment variable
    char aks_home[MAX_PATH];
    if (GetEnvironmentVariableA("AKS_HOME", aks_home, sizeof(aks_home)) > 0 && strlen(aks_home) > 0) {
        char test_exe[MAX_PATH];
        snprintf(test_exe, sizeof(test_exe), "%s\\bin\\aks.exe", aks_home);
        if (_access(test_exe, 0) == 0) {
            snprintf(out_dir, max_len, "%s", aks_home);
            return true;
        }
    }

    return false;
}

static bool EnsureDirectory(const char *path) {
    char full[MAX_PATH];
    GetFullPathNameA(path, MAX_PATH, full, NULL);
    int res = SHCreateDirectoryExA(NULL, full, NULL);
    return (res == ERROR_SUCCESS || res == ERROR_FILE_EXISTS || res == ERROR_ALREADY_EXISTS);
}

static bool CopySingleFile(const char *src, const char *dst) {
    char parent[MAX_PATH];
    strncpy(parent, dst, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char *last_slash = strrchr(parent, '\\');
    if (!last_slash) last_slash = strrchr(parent, '/');
    if (last_slash) {
        *last_slash = '\0';
        EnsureDirectory(parent);
    }
    return CopyFileA(src, dst, FALSE) != 0;
}

static bool AddToUserPath(const char *bin_dir) {
    HKEY hKey;
    LONG res = RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ | KEY_WRITE, &hKey);
    if (res != ERROR_SUCCESS) return false;

    char current_path[32768] = {0};
    DWORD path_len = sizeof(current_path);
    DWORD type = REG_EXPAND_SZ;
    res = RegQueryValueExA(hKey, "Path", NULL, &type, (LPBYTE)current_path, &path_len);
    if (res != ERROR_SUCCESS) current_path[0] = '\0';

    if (strstr(current_path, bin_dir) != NULL) {
        RegCloseKey(hKey);
        return true;
    }

    char new_path[32768];
    if (strlen(current_path) > 0) {
        size_t clen = strlen(current_path);
        if (current_path[clen - 1] == ';') {
            snprintf(new_path, sizeof(new_path), "%s%s", current_path, bin_dir);
        } else {
            snprintf(new_path, sizeof(new_path), "%s;%s", current_path, bin_dir);
        }
    } else {
        snprintf(new_path, sizeof(new_path), "%s", bin_dir);
    }

    res = RegSetValueExA(hKey, "Path", 0, REG_EXPAND_SZ, (const BYTE *)new_path, (DWORD)(strlen(new_path) + 1));
    RegCloseKey(hKey);

    DWORD_PTR dwResult = 0;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, &dwResult);
    return res == ERROR_SUCCESS;
}

static bool RemoveFromUserPath(const char *bin_dir) {
    HKEY hKey;
    LONG res = RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ | KEY_WRITE, &hKey);
    if (res != ERROR_SUCCESS) return false;

    char current_path[32768] = {0};
    DWORD path_len = sizeof(current_path);
    DWORD type = REG_EXPAND_SZ;
    res = RegQueryValueExA(hKey, "Path", NULL, &type, (LPBYTE)current_path, &path_len);
    if (res != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }

    char new_path[32768] = {0};
    char *token = strtok(current_path, ";");
    bool first = true;
    while (token) {
        if (_stricmp(token, bin_dir) != 0 && strlen(token) > 0) {
            if (!first) strcat(new_path, ";");
            strcat(new_path, token);
            first = false;
        }
        token = strtok(NULL, ";");
    }

    res = RegSetValueExA(hKey, "Path", 0, REG_EXPAND_SZ, (const BYTE *)new_path, (DWORD)(strlen(new_path) + 1));
    RegCloseKey(hKey);

    DWORD_PTR dwResult = 0;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, &dwResult);
    return res == ERROR_SUCCESS;
}

static bool SetAksHome(const char *install_dir) {
    HKEY hKey;
    LONG res = RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_WRITE, &hKey);
    if (res != ERROR_SUCCESS) return false;
    res = RegSetValueExA(hKey, "AKS_HOME", 0, REG_SZ, (const BYTE *)install_dir, (DWORD)(strlen(install_dir) + 1));
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

static bool RemoveAksHome(void) {
    HKEY hKey;
    LONG res = RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_WRITE, &hKey);
    if (res != ERROR_SUCCESS) return false;
    RegDeleteValueA(hKey, "AKS_HOME");
    RegCloseKey(hKey);
    return true;
}

static bool RegisterFileAssociation(const char *aks_exe) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\.aks", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        const char *prog_id = "AksSourceFile";
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *)prog_id, (DWORD)(strlen(prog_id) + 1));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\AksSourceFile", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        const char *desc = "AKS Source File";
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *)desc, (DWORD)(strlen(desc) + 1));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\AksSourceFile\\DefaultIcon", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        char icon_str[MAX_PATH + 8];
        snprintf(icon_str, sizeof(icon_str), "\"%s\",0", aks_exe);
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *)icon_str, (DWORD)(strlen(icon_str) + 1));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\AksSourceFile\\shell\\open\\command", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        char cmd_str[MAX_PATH * 2];
        snprintf(cmd_str, sizeof(cmd_str), "\"%s\" run \"%%1\"", aks_exe);
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *)cmd_str, (DWORD)(strlen(cmd_str) + 1));
        RegCloseKey(hKey);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return true;
}

static bool UnregisterFileAssociation(void) {
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\.aks");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\AksSourceFile\\shell\\open\\command");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\AksSourceFile\\shell\\open");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\AksSourceFile\\shell");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\AksSourceFile\\DefaultIcon");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\AksSourceFile");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return true;
}

static bool RegisterUninstallEntry(const char *install_dir, const char *uninstaller_exe, const char *aks_exe) {
    HKEY hKey;
    const char *key_path = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AKSLang";
    if (RegCreateKeyExA(HKEY_CURRENT_USER, key_path, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return false;
    }
    const char *name = "AKS Programming Language";
    const char *version = AKS_VERSION;
    const char *publisher = "AKS Language Authors";
    const char *url = "https://github.com/aks-lang/aks-lang";
    char uninst_cmd[MAX_PATH + 8];
    char quiet_cmd[MAX_PATH + 16];
    char icon_cmd[MAX_PATH + 8];

    snprintf(uninst_cmd, sizeof(uninst_cmd), "\"%s\"", uninstaller_exe);
    snprintf(quiet_cmd, sizeof(quiet_cmd), "\"%s\" --silent", uninstaller_exe);
    snprintf(icon_cmd, sizeof(icon_cmd), "\"%s\",0", aks_exe);

    RegSetValueExA(hKey, "DisplayName", 0, REG_SZ, (const BYTE *)name, (DWORD)(strlen(name) + 1));
    RegSetValueExA(hKey, "DisplayVersion", 0, REG_SZ, (const BYTE *)version, (DWORD)(strlen(version) + 1));
    RegSetValueExA(hKey, "Publisher", 0, REG_SZ, (const BYTE *)publisher, (DWORD)(strlen(publisher) + 1));
    RegSetValueExA(hKey, "InstallLocation", 0, REG_SZ, (const BYTE *)install_dir, (DWORD)(strlen(install_dir) + 1));
    RegSetValueExA(hKey, "UninstallString", 0, REG_SZ, (const BYTE *)uninst_cmd, (DWORD)(strlen(uninst_cmd) + 1));
    RegSetValueExA(hKey, "QuietUninstallString", 0, REG_SZ, (const BYTE *)quiet_cmd, (DWORD)(strlen(quiet_cmd) + 1));
    RegSetValueExA(hKey, "DisplayIcon", 0, REG_SZ, (const BYTE *)icon_cmd, (DWORD)(strlen(icon_cmd) + 1));
    RegSetValueExA(hKey, "HelpLink", 0, REG_SZ, (const BYTE *)url, (DWORD)(strlen(url) + 1));
    RegSetValueExA(hKey, "URLInfoAbout", 0, REG_SZ, (const BYTE *)url, (DWORD)(strlen(url) + 1));

    DWORD estimated_size_kb = 28000;
    RegSetValueExA(hKey, "EstimatedSize", 0, REG_DWORD, (const BYTE *)&estimated_size_kb, sizeof(DWORD));
    RegCloseKey(hKey);
    return true;
}

static bool UnregisterUninstallEntry(void) {
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AKSLang");
    return true;
}

static bool DeleteDirectoryRecursively(const char *dir) {
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*.*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char file_path[MAX_PATH];
        snprintf(file_path, sizeof(file_path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DeleteDirectoryRecursively(file_path);
        } else {
            SetFileAttributesA(file_path, FILE_ATTRIBUTE_NORMAL);
            DeleteFileA(file_path);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    return RemoveDirectoryA(dir) != 0;
}

static bool ExtractDistribution(const char *self_path, const char *dest_dir) {
    EnsureDirectory(dest_dir);
    char bin_dir[MAX_PATH];
    snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", dest_dir);
    EnsureDirectory(bin_dir);

    FILE *f = fopen(self_path, "rb");
    bool extracted = false;
    if (f) {
        _fseeki64(f, 0, SEEK_END);
        int64_t file_len = _ftelli64(f);
        if (file_len >= (int64_t)sizeof(AksPkgTrailer)) {
            _fseeki64(f, file_len - sizeof(AksPkgTrailer), SEEK_SET);
            AksPkgTrailer trailer;
            if (fread(&trailer, sizeof(trailer), 1, f) == 1 && memcmp(trailer.magic, AKS_MAGIC, 8) == 0) {
                _fseeki64(f, (int64_t)trailer.toc_offset, SEEK_SET);
                AksPkgFileEntry *entries = (AksPkgFileEntry *)malloc(trailer.file_count * sizeof(AksPkgFileEntry));
                if (entries && fread(entries, sizeof(AksPkgFileEntry), trailer.file_count, f) == trailer.file_count) {
                    char buf[65536];
                    for (uint32_t i = 0; i < trailer.file_count; i++) {
                        g_installProgressPct = (int)((i + 1) * 75 / trailer.file_count);
                        snprintf(g_installCurrentFile, sizeof(g_installCurrentFile), "%s", entries[i].rel_path);
                        snprintf(g_installStepText, sizeof(g_installStepText), "Extracting component %u of %u...", i + 1, trailer.file_count);
                        if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);

                        char out_path[MAX_PATH];
                        snprintf(out_path, sizeof(out_path), "%s\\%s", dest_dir, entries[i].rel_path);

                        for (char *p = out_path; *p; p++) {
                            if (*p == '/') *p = '\\';
                        }

                        char parent[MAX_PATH];
                        strncpy(parent, out_path, sizeof(parent) - 1);
                        parent[sizeof(parent) - 1] = '\0';
                        char *last_sep = strrchr(parent, '\\');
                        if (last_sep) {
                            *last_sep = '\0';
                            EnsureDirectory(parent);
                        }

                        if (_fseeki64(f, (int64_t)entries[i].file_offset, SEEK_SET) == 0) {
                            FILE *out_f = fopen(out_path, "wb");
                            if (out_f) {
                                uint64_t rem = entries[i].file_size;
                                while (rem > 0) {
                                    size_t chunk = (rem > sizeof(buf)) ? sizeof(buf) : (size_t)rem;
                                    size_t n = fread(buf, 1, chunk, f);
                                    if (n == 0) break;
                                    fwrite(buf, 1, n, out_f);
                                    rem -= n;
                                }
                                fclose(out_f);
                            }
                        }
                        Sleep(6);
                    }
                    free(entries);
                    extracted = true;
                }
            }
        }
        fclose(f);
    }

    if (!extracted) {
        snprintf(g_installStepText, sizeof(g_installStepText), "Copying distribution components...");
        char repo_dir[MAX_PATH];
        strncpy(repo_dir, self_path, sizeof(repo_dir) - 1);
        char *sep = strrchr(repo_dir, '\\');
        if (sep) *sep = '\0';

        const char *binaries[] = {"aks.exe", "aksls.exe", "aksfmt.exe", "akslint.exe", "aks.dll"};
        for (size_t i = 0; i < 5; i++) {
            char s[MAX_PATH], d[MAX_PATH];
            snprintf(s, sizeof(s), "%s\\%s", repo_dir, binaries[i]);
            snprintf(d, sizeof(d), "%s\\bin\\%s", dest_dir, binaries[i]);
            CopySingleFile(s, d);
            g_installProgressPct = (int)((i + 1) * 15);
            snprintf(g_installCurrentFile, sizeof(g_installCurrentFile), "bin\\%s", binaries[i]);
            if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
            Sleep(20);
        }
    }
    return true;
}

// Run a background command silently with NO flashing console window
static bool RunProcessSilently(const char *command_line) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    char full_cmd[MAX_PATH * 3];
    snprintf(full_cmd, sizeof(full_cmd), "cmd.exe /c %s", command_line);

    if (CreateProcessA(NULL, full_cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

// Background Worker Thread for Installation / Repair
static DWORD WINAPI InstallWorkerThread(LPVOID lpParam) {
    (void)lpParam;
    EnsureDirectory(g_targetDir);

    char bin_dir[MAX_PATH];
    snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", g_targetDir);
    EnsureDirectory(bin_dir);

    ExtractDistribution(g_selfPath, g_targetDir);

    // Install uninstaller executable
    snprintf(g_installStepText, sizeof(g_installStepText), "Setting up uninstaller...");
    g_installProgressPct = 80;
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);

    char uninst_path[MAX_PATH];
    snprintf(uninst_path, sizeof(uninst_path), "%s\\bin\\aks-uninstaller.exe", g_targetDir);
    CopyFileA(g_selfPath, uninst_path, FALSE);

    char aks_exe[MAX_PATH];
    snprintf(aks_exe, sizeof(aks_exe), "%s\\bin\\aks.exe", g_targetDir);

    // Register PATH
    if (g_optPath) {
        snprintf(g_installStepText, sizeof(g_installStepText), "Updating User PATH environment variable...");
        g_installProgressPct = 88;
        if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
        AddToUserPath(bin_dir);
        SetAksHome(g_targetDir);
    }

    // File Associations
    if (g_optAssoc) {
        snprintf(g_installStepText, sizeof(g_installStepText), "Registering .aks file association...");
        g_installProgressPct = 93;
        if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
        RegisterFileAssociation(aks_exe);
    }

    // Register uninstaller in Windows Settings
    snprintf(g_installStepText, sizeof(g_installStepText), "Registering in Windows Installed Apps...");
    g_installProgressPct = 97;
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
    RegisterUninstallEntry(g_targetDir, uninst_path, aks_exe);

    // Optional VS Code extension (100% silent execution)
    if (g_optVSCode) {
        snprintf(g_installStepText, sizeof(g_installStepText), "Setting up VS Code extension...");
        if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
        char vsix_path[MAX_PATH];
        snprintf(vsix_path, sizeof(vsix_path), "%s\\editors\\vscode\\aks-lang-1.0.0.vsix", g_targetDir);
        if (_access(vsix_path, 0) == 0) {
            char cmd[MAX_PATH * 2];
            snprintf(cmd, sizeof(cmd), "code --install-extension \"%s\" --force", vsix_path);
            RunProcessSilently(cmd);
        }
    }

    g_installProgressPct = 100;
    snprintf(g_installStepText, sizeof(g_installStepText), g_isRepairMode ? "Repair complete!" : "Installation complete!");
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
    Sleep(300);

    PostMessage(g_hMainWnd, WM_USER + 100, 0, 0);
    return 0;
}

// Background Worker Thread for Uninstallation
static DWORD WINAPI UninstallWorkerThread(LPVOID lpParam) {
    (void)lpParam;
    char bin_dir[MAX_PATH];
    snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", g_targetDir);

    snprintf(g_installStepText, sizeof(g_installStepText), "Removing PATH environment variables...");
    g_installProgressPct = 30;
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
    RemoveFromUserPath(bin_dir);
    RemoveAksHome();
    Sleep(150);

    snprintf(g_installStepText, sizeof(g_installStepText), "Removing file associations & registry entries...");
    g_installProgressPct = 65;
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
    UnregisterFileAssociation();
    UnregisterUninstallEntry();
    Sleep(150);

    snprintf(g_installStepText, sizeof(g_installStepText), "Deleting installed files...");
    g_installProgressPct = 95;
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
    DeleteDirectoryRecursively(g_targetDir);
    Sleep(150);

    g_installProgressPct = 100;
    snprintf(g_installStepText, sizeof(g_installStepText), "Uninstallation completed successfully.");
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
    Sleep(200);

    PostMessage(g_hMainWnd, WM_USER + 101, 0, 0);
    return 0;
}

static void ShowPage(WizardPage page) {
    g_currentPage = page;
    if (g_hEditPath) {
        ShowWindow(g_hEditPath, (page == PAGE_OPTIONS) ? SW_SHOW : SW_HIDE);
    }
    if (page == PAGE_INSTALLING) {
        g_installProgressPct = 0;
        if (g_isRepairMode) {
            snprintf(g_installStepText, sizeof(g_installStepText), "Verifying and repairing installation...");
        } else {
            snprintf(g_installStepText, sizeof(g_installStepText), "Starting installation...");
        }
        CreateThread(NULL, 0, InstallWorkerThread, NULL, 0, NULL);
    } else if (page == PAGE_UNINSTALLING) {
        g_installProgressPct = 0;
        snprintf(g_installStepText, sizeof(g_installStepText), "Starting uninstallation...");
        CreateThread(NULL, 0, UninstallWorkerThread, NULL, 0, NULL);
    }
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
}

// Drawing Helpers
static void DrawRoundedRect(Gdiplus::Graphics &g, Gdiplus::Pen *pen, Gdiplus::Brush *brush, float x, float y, float w, float h, float r) {
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, r * 2.0f, r * 2.0f, 180.0f, 90.0f);
    path.AddArc(x + w - r * 2.0f, y, r * 2.0f, r * 2.0f, 270.0f, 90.0f);
    path.AddArc(x + w - r * 2.0f, y + h - r * 2.0f, r * 2.0f, r * 2.0f, 0.0f, 90.0f);
    path.AddArc(x, y + h - r * 2.0f, r * 2.0f, r * 2.0f, 90.0f, 90.0f);
    path.CloseFigure();
    if (brush) g.FillPath(brush, &path);
    if (pen) g.DrawPath(pen, &path);
}

static void DrawModernButton(Gdiplus::Graphics &g, const RECT &rc, const wchar_t *text, bool isPrimary, bool isHover, bool isPressed, const AppTheme &theme, Gdiplus::Font *font) {
    float x = (float)rc.left, y = (float)rc.top, w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
    float r = 5.0f;

    if (isPrimary) {
        Gdiplus::Color bg = isPressed ? theme.btnPrimaryPressed : (isHover ? theme.btnPrimaryHover : theme.btnPrimaryBg);
        Gdiplus::SolidBrush brush(bg);
        DrawRoundedRect(g, NULL, &brush, x, y, w, h, r);

        Gdiplus::SolidBrush textBrush(theme.btnPrimaryText);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layoutRect(x, y, w, h);
        g.DrawString(text, -1, font, layoutRect, &sf, &textBrush);
    } else {
        Gdiplus::Color bg = isPressed ? theme.divider : (isHover ? theme.btnSecondaryHover : theme.btnSecondaryBg);
        Gdiplus::SolidBrush brush(bg);
        Gdiplus::Pen borderPen(theme.btnSecondaryBorder, 1.0f);
        DrawRoundedRect(g, &borderPen, &brush, x, y, w, h, r);

        Gdiplus::SolidBrush textBrush(theme.btnSecondaryText);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layoutRect(x, y, w, h);
        g.DrawString(text, -1, font, layoutRect, &sf, &textBrush);
    }
}

static void DrawModernCheckbox(Gdiplus::Graphics &g, float x, float y, bool isChecked, bool isHover, const AppTheme &theme) {
    float size = 16.0f;
    float r = 3.5f;
    if (isChecked) {
        Gdiplus::SolidBrush fillBrush(theme.checkBgChecked);
        DrawRoundedRect(g, NULL, &fillBrush, x, y, size, size, r);

        Gdiplus::Pen checkPen(theme.checkMark, 2.0f);
        checkPen.SetStartCap(Gdiplus::LineCapRound);
        checkPen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&checkPen, x + 3.5f, y + 8.5f, x + 6.5f, y + 11.5f);
        g.DrawLine(&checkPen, x + 6.5f, y + 11.5f, x + 12.5f, y + 4.5f);
    } else {
        Gdiplus::SolidBrush fillBrush(theme.checkBgUnchecked);
        Gdiplus::Pen pen(isHover ? theme.checkBgChecked : theme.checkBorder, 1.5f);
        DrawRoundedRect(g, &pen, &fillBrush, x, y, size, size, r);
    }
}

// Window Procedure
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMainWnd = hWnd;
            g_dpiScale = DetectCurrentDpiScale(hWnd);
            g_isDarkMode = DetectIsDarkMode();
            ApplyImmersiveDarkMode(hWnd, g_isDarkMode);

            // Initialize GDI+ Fonts with UnitPixel (integer pixel heights, zero fractional grid distortion)
            g_pFontTitle    = new Gdiplus::Font(L"Segoe UI", 20.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            g_pFontHeading  = new Gdiplus::Font(L"Segoe UI", 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            g_pFontBody     = new Gdiplus::Font(L"Segoe UI", 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            g_pFontBodyBold = new Gdiplus::Font(L"Segoe UI", 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            g_pFontSub      = new Gdiplus::Font(L"Segoe UI", 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            g_pFontMono     = new Gdiplus::Font(L"Consolas", 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            g_pFontBtn      = new Gdiplus::Font(L"Segoe UI", 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            g_pFontChevron  = new Gdiplus::Font(L"Segoe UI", 18.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

            g_hFontNormal   = CreateFontW((int)(-13.0f * g_dpiScale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

            AppTheme theme = GetCurrentTheme(g_isDarkMode);
            g_hEditBrush = CreateSolidBrush(theme.editBg);

            // Load emblem and full background logos
            g_pLogoBitmap          = LoadPngFromMemory(g_aks_logo_png, g_aks_logo_png_size);
            g_pLogoFullDarkBitmap  = LoadPngFromMemory(g_aks_full_logo_dark_png, g_aks_full_logo_dark_png_size);
            g_pLogoFullLightBitmap = LoadPngFromMemory(g_aks_full_logo_light_png, g_aks_full_logo_light_png_size);

            // Window Icon: Try resource icon first (sharp 32-bit alpha icon), fallback to GDI+
            HICON hIconBig = (HICON)LoadImageA(g_hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
            HICON hIconSm  = (HICON)LoadImageA(g_hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
            if (hIconBig) SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
            if (hIconSm)  SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
            if (!hIconBig && g_pLogoBitmap) {
                HICON hLogoIcon = NULL;
                if (g_pLogoBitmap->GetHICON(&hLogoIcon) == Gdiplus::Ok && hLogoIcon) {
                    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hLogoIcon);
                    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hLogoIcon);
                }
            }

            // Flat Borderless Native Edit control embedded inside container (centered at y: 134-168)
            int editX = (int)(50.0f * g_dpiScale);
            int editY = (int)(140.0f * g_dpiScale);
            int editW = (int)(390.0f * g_dpiScale);
            int editH = (int)(22.0f * g_dpiScale);
            g_hEditPath = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, editX, editY, editW, editH, hWnd, (HMENU)IDC_EDIT_PATH, g_hInstance, NULL);
            SendMessage(g_hEditPath, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            SendMessage(g_hEditPath, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
            SetWindowTextA(g_hEditPath, g_targetDir);

            ShowPage(g_isUninstallMode ? PAGE_UNINSTALL : (g_isAlreadyInstalled ? PAGE_MAINTENANCE : PAGE_OPTIONS));
            break;
        }

        case WM_DPICHANGED: {
            g_dpiScale = (float)HIWORD(wParam) / 96.0f;
            RECT *prcNewWindow = (RECT *)lParam;
            SetWindowPos(hWnd, NULL,
                prcNewWindow->left, prcNewWindow->top,
                prcNewWindow->right - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOACTIVATE);

            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            g_hFontNormal = CreateFontW((int)(-13.0f * g_dpiScale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            if (g_hEditPath) {
                SendMessage(g_hEditPath, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SetWindowPos(g_hEditPath, NULL,
                    (int)(50.0f * g_dpiScale), (int)(140.0f * g_dpiScale),
                    (int)(390.0f * g_dpiScale), (int)(22.0f * g_dpiScale),
                    SWP_NOZORDER);
            }
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        }

        case WM_SETTINGCHANGE: {
            bool newDark = DetectIsDarkMode();
            if (newDark != g_isDarkMode) {
                g_isDarkMode = newDark;
                ApplyImmersiveDarkMode(hWnd, g_isDarkMode);
                AppTheme theme = GetCurrentTheme(g_isDarkMode);
                if (g_hEditBrush) DeleteObject(g_hEditBrush);
                g_hEditBrush = CreateSolidBrush(theme.editBg);
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            AppTheme theme = GetCurrentTheme(g_isDarkMode);
            SetTextColor(hdcEdit, theme.editText);
            SetBkColor(hdcEdit, theme.editBg);
            return (LRESULT)g_hEditBrush;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (g_hoverBrowse || g_hoverRowPath || g_hoverRowAssoc || g_hoverRowVSCode || g_hoverRowTerm ||
                    g_hoverCancel || g_hoverInstall || g_hoverFinish || g_hoverRepair || g_hoverUninstall || g_hoverClose) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
            }
            return DefWindowProcA(hWnd, msg, wParam, lParam);
        }

        case WM_CLOSE: {
            if (g_currentPage == PAGE_INSTALLING || g_currentPage == PAGE_UNINSTALLING) {
                if (MessageBoxA(hWnd, "Installation is in progress. Are you sure you want to cancel?", "Exit Installer", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                    return 0;
                }
            }
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            int physX = GET_X_LPARAM(lParam);
            int physY = GET_Y_LPARAM(lParam);
            int x = (int)((float)physX / g_dpiScale);
            int y = (int)((float)physY / g_dpiScale);
            POINT pt = {x, y};

            bool oldHoverBrowse    = g_hoverBrowse;
            bool oldHoverRowPath   = g_hoverRowPath;
            bool oldHoverRowAssoc  = g_hoverRowAssoc;
            bool oldHoverRowVSCode = g_hoverRowVSCode;
            bool oldHoverRowTerm   = g_hoverRowTerm;
            bool oldHoverRepair    = g_hoverRepair;
            bool oldHoverUninstall = g_hoverUninstall;
            bool oldHoverClose     = g_hoverClose;
            bool oldHoverCancel    = g_hoverCancel;
            bool oldHoverInstall   = g_hoverInstall;
            bool oldHoverFinish    = g_hoverFinish;

            g_hoverBrowse    = (g_currentPage == PAGE_OPTIONS) && PtInRect(&RC_BTN_BROWSE, pt);
            g_hoverRowPath   = (g_currentPage == PAGE_OPTIONS) && PtInRect(&RC_ROW_PATH, pt);
            g_hoverRowAssoc  = (g_currentPage == PAGE_OPTIONS) && PtInRect(&RC_ROW_ASSOC, pt);
            g_hoverRowVSCode = (g_currentPage == PAGE_OPTIONS) && PtInRect(&RC_ROW_VSCODE, pt);
            g_hoverRowTerm   = (g_currentPage == PAGE_COMPLETED) && PtInRect(&RC_ROW_TERM, pt);
            g_hoverRepair    = (g_currentPage == PAGE_MAINTENANCE) && PtInRect(&RC_CARD_REPAIR, pt);
            g_hoverUninstall = (g_currentPage == PAGE_MAINTENANCE) && PtInRect(&RC_CARD_UNINSTALL, pt);
            g_hoverClose     = (g_currentPage == PAGE_MAINTENANCE) && PtInRect(&RC_BTN_CLOSE, pt);
            g_hoverCancel    = (g_currentPage == PAGE_OPTIONS || g_currentPage == PAGE_UNINSTALL) && PtInRect(&RC_BTN_CANCEL, pt);
            g_hoverInstall   = (g_currentPage == PAGE_OPTIONS || g_currentPage == PAGE_UNINSTALL) && PtInRect(&RC_BTN_INSTALL, pt);
            g_hoverFinish    = (g_currentPage == PAGE_COMPLETED || g_currentPage == PAGE_UNINSTALLED) && PtInRect(&RC_BTN_FINISH, pt);

            TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0};
            TrackMouseEvent(&tme);

            bool changed = (oldHoverBrowse != g_hoverBrowse) ||
                           (oldHoverRowPath != g_hoverRowPath) ||
                           (oldHoverRowAssoc != g_hoverRowAssoc) ||
                           (oldHoverRowVSCode != g_hoverRowVSCode) ||
                           (oldHoverRowTerm != g_hoverRowTerm) ||
                           (oldHoverRepair != g_hoverRepair) ||
                           (oldHoverUninstall != g_hoverUninstall) ||
                           (oldHoverClose != g_hoverClose) ||
                           (oldHoverCancel != g_hoverCancel) ||
                           (oldHoverInstall != g_hoverInstall) ||
                           (oldHoverFinish != g_hoverFinish);

            if (changed) {
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        }

        case WM_MOUSELEAVE: {
            g_hoverBrowse = false;
            g_hoverRowPath = false;
            g_hoverRowAssoc = false;
            g_hoverRowVSCode = false;
            g_hoverRowTerm = false;
            g_hoverRepair = false;
            g_hoverUninstall = false;
            g_hoverClose = false;
            g_hoverCancel = false;
            g_hoverInstall = false;
            g_hoverFinish = false;
            g_pressBrowse = false;
            g_pressRepair = false;
            g_pressUninstall = false;
            g_pressClose = false;
            g_pressCancel = false;
            g_pressInstall = false;
            g_pressFinish = false;
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        case WM_LBUTTONDOWN: {
            int physX = GET_X_LPARAM(lParam);
            int physY = GET_Y_LPARAM(lParam);
            int x = (int)((float)physX / g_dpiScale);
            int y = (int)((float)physY / g_dpiScale);
            POINT pt = {x, y};

            if (g_currentPage == PAGE_OPTIONS) {
                if (PtInRect(&RC_ROW_PATH, pt)) {
                    g_optPath = !g_optPath;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_ROW_ASSOC, pt)) {
                    g_optAssoc = !g_optAssoc;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_ROW_VSCODE, pt)) {
                    g_optVSCode = !g_optVSCode;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_BTN_BROWSE, pt)) {
                    g_pressBrowse = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_BTN_INSTALL, pt)) {
                    g_pressInstall = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_BTN_CANCEL, pt)) {
                    g_pressCancel = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            } else if (g_currentPage == PAGE_MAINTENANCE) {
                if (PtInRect(&RC_CARD_REPAIR, pt)) {
                    g_pressRepair = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_CARD_UNINSTALL, pt)) {
                    g_pressUninstall = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_BTN_CLOSE, pt)) {
                    g_pressClose = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            } else if (g_currentPage == PAGE_COMPLETED) {
                if (PtInRect(&RC_ROW_TERM, pt)) {
                    g_optOpenTerm = !g_optOpenTerm;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_BTN_FINISH, pt)) {
                    g_pressFinish = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            } else if (g_currentPage == PAGE_UNINSTALL) {
                if (PtInRect(&RC_BTN_INSTALL, pt)) {
                    g_pressInstall = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (PtInRect(&RC_BTN_CANCEL, pt)) {
                    g_pressCancel = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            } else if (g_currentPage == PAGE_UNINSTALLED) {
                if (PtInRect(&RC_BTN_FINISH, pt)) {
                    g_pressFinish = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            break;
        }

        case WM_LBUTTONUP: {
            int physX = GET_X_LPARAM(lParam);
            int physY = GET_Y_LPARAM(lParam);
            int x = (int)((float)physX / g_dpiScale);
            int y = (int)((float)physY / g_dpiScale);
            POINT pt = {x, y};

            if (g_pressBrowse && PtInRect(&RC_BTN_BROWSE, pt)) {
                BROWSEINFOA bi = {0};
                bi.hwndOwner = hWnd;
                bi.lpszTitle = "Select Destination Folder for AKS:";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
                if (pidl) {
                    char chosen[MAX_PATH];
                    if (SHGetPathFromIDListA(pidl, chosen)) {
                        snprintf(g_targetDir, sizeof(g_targetDir), "%s\\%s", chosen, DEFAULT_APP_NAME);
                        SetWindowTextA(g_hEditPath, g_targetDir);
                    }
                    CoTaskMemFree(pidl);
                }
            } else if (g_pressRepair && PtInRect(&RC_CARD_REPAIR, pt)) {
                g_isRepairMode = true;
                g_optPath = true;
                g_optAssoc = true;
                g_optVSCode = true;
                ShowPage(PAGE_INSTALLING);
            } else if (g_pressUninstall && PtInRect(&RC_CARD_UNINSTALL, pt)) {
                ShowPage(PAGE_UNINSTALL);
            } else if (g_pressClose && PtInRect(&RC_BTN_CLOSE, pt)) {
                DestroyWindow(hWnd);
            } else if (g_pressInstall && PtInRect(&RC_BTN_INSTALL, pt)) {
                if (g_currentPage == PAGE_OPTIONS) {
                    GetWindowTextA(g_hEditPath, g_targetDir, sizeof(g_targetDir));
                    ShowPage(PAGE_INSTALLING);
                } else if (g_currentPage == PAGE_UNINSTALL) {
                    ShowPage(PAGE_UNINSTALLING);
                }
            } else if (g_pressCancel && PtInRect(&RC_BTN_CANCEL, pt)) {
                if (g_currentPage == PAGE_UNINSTALL && g_isAlreadyInstalled && !g_isUninstallMode) {
                    ShowPage(PAGE_MAINTENANCE);
                } else {
                    DestroyWindow(hWnd);
                }
            } else if (g_pressFinish && PtInRect(&RC_BTN_FINISH, pt)) {
                if (g_currentPage == PAGE_COMPLETED) {
                    if (g_optOpenTerm) {
                        char bin_dir[MAX_PATH];
                        snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", g_targetDir);
                        char cmd[MAX_PATH * 2];
                        snprintf(cmd, sizeof(cmd), "/k set \"PATH=%s;%%PATH%%\" && aks help", bin_dir);
                        ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_SHOW);
                    }
                } else if (g_currentPage == PAGE_UNINSTALLED) {
                    char cmd[MAX_PATH * 3];
                    snprintf(cmd, sizeof(cmd), "/c ping 127.0.0.1 -n 2 >nul & rmdir /s /q \"%s\"", g_targetDir);
                    ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
                }
                DestroyWindow(hWnd);
            }

            g_pressBrowse = false;
            g_pressRepair = false;
            g_pressUninstall = false;
            g_pressClose = false;
            g_pressCancel = false;
            g_pressInstall = false;
            g_pressFinish = false;
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            int width = clientRect.right - clientRect.left;
            int height = clientRect.bottom - clientRect.top;

            AppTheme theme = GetCurrentTheme(g_isDarkMode);

            // Double buffering
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            {
                Gdiplus::Graphics g(memDC);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

                // 1. Clean Background
                Gdiplus::SolidBrush bgBrush(theme.bg);
                g.FillRectangle(&bgBrush, 0, 0, width, height);

                // ScaleTransform maps logical 580x440 layout directly to native physical device pixels
                g.ScaleTransform(g_dpiScale, g_dpiScale);

                // 1.5 Atmospheric Ambient Watermark in Background (Full AKS Logo with Typography)
                Gdiplus::Bitmap *pBgLogo = g_isDarkMode ? g_pLogoFullDarkBitmap : g_pLogoFullLightBitmap;
                if (!pBgLogo) pBgLogo = g_pLogoBitmap;
                if (pBgLogo) {
                    float bgW = 220.0f;
                    float bgH = (float)pBgLogo->GetHeight() * (bgW / (float)pBgLogo->GetWidth());
                    float bgX = (580.0f - bgW) / 2.0f;
                    float bgY = 70.0f;

                    Gdiplus::ColorMatrix cm = {
                        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, g_isDarkMode ? 0.08f : 0.05f, 0.0f,
                        0.0f, 0.0f, 0.0f, 0.0f, 1.0f
                    };
                    Gdiplus::ImageAttributes ia;
                    ia.SetColorMatrix(&cm, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

                    g.DrawImage(pBgLogo,
                                Gdiplus::RectF(bgX, bgY, bgW, bgH),
                                0.0f, 0.0f, (float)pBgLogo->GetWidth(), (float)pBgLogo->GetHeight(),
                                Gdiplus::UnitPixel, &ia);
                }

                // 2. Header Area (Clean, Left-Aligned Typography)
                Gdiplus::SolidBrush titleBrush(theme.textTitle);
                Gdiplus::SolidBrush mutedBrush(theme.textMuted);
                Gdiplus::SolidBrush headingBrush(theme.textHeading);
                Gdiplus::SolidBrush bodyBrush(theme.textBody);

                if (g_currentPage == PAGE_MAINTENANCE) {
                    g.DrawString(L"Modify Installation", -1, g_pFontTitle, Gdiplus::PointF(40.0f, 20.0f), &titleBrush);
                    wchar_t wMaintSub[128];
                    swprintf_s(wMaintSub, L"AKS Programming Language %s is currently installed.", AKS_VERSION_WIDE);
                    g.DrawString(wMaintSub, -1, g_pFontSub, Gdiplus::PointF(40.0f, 48.0f), &mutedBrush);
                } else if (g_currentPage == PAGE_INSTALLING) {
                    g.DrawString(g_isRepairMode ? L"Repairing AKS Programming Language" : L"Installing AKS Programming Language", -1, g_pFontTitle, Gdiplus::PointF(40.0f, 20.0f), &titleBrush);
                    g.DrawString(L"Please wait while files and toolchain components are configured.", -1, g_pFontSub, Gdiplus::PointF(40.0f, 48.0f), &mutedBrush);
                } else if (g_currentPage == PAGE_COMPLETED) {
                    g.DrawString(g_isRepairMode ? L"Repair Completed Successfully" : L"Installation Completed Successfully", -1, g_pFontTitle, Gdiplus::PointF(40.0f, 20.0f), &titleBrush);
                    g.DrawString(g_isRepairMode ? L"All AKS toolchain components and PATH settings have been restored." : L"AKS Programming Language is ready to use from your terminal.", -1, g_pFontSub, Gdiplus::PointF(40.0f, 48.0f), &mutedBrush);
                } else if (g_currentPage == PAGE_UNINSTALL) {
                    g.DrawString(L"Uninstall AKS Programming Language", -1, g_pFontTitle, Gdiplus::PointF(40.0f, 20.0f), &titleBrush);
                    g.DrawString(L"Remove AKS compiler, runtime, and tools from this computer.", -1, g_pFontSub, Gdiplus::PointF(40.0f, 48.0f), &mutedBrush);
                } else if (g_currentPage == PAGE_UNINSTALLING) {
                    g.DrawString(L"Uninstalling AKS Programming Language", -1, g_pFontTitle, Gdiplus::PointF(40.0f, 20.0f), &titleBrush);
                    g.DrawString(L"Please wait while components and registry keys are removed.", -1, g_pFontSub, Gdiplus::PointF(40.0f, 48.0f), &mutedBrush);
                } else if (g_currentPage == PAGE_UNINSTALLED) {
                    g.DrawString(L"Uninstallation Completed", -1, g_pFontTitle, Gdiplus::PointF(40.0f, 20.0f), &titleBrush);
                    g.DrawString(L"AKS Programming Language has been cleanly removed from your computer.", -1, g_pFontSub, Gdiplus::PointF(40.0f, 48.0f), &mutedBrush);
                } else {
                    g.DrawString(L"Install AKS Programming Language", -1, g_pFontTitle, Gdiplus::PointF(40.0f, 20.0f), &titleBrush);
                    wchar_t wVerSub[128];
                    swprintf_s(wVerSub, L"Version %s (64-bit)", AKS_VERSION_WIDE);
                    g.DrawString(wVerSub, -1, g_pFontSub, Gdiplus::PointF(40.0f, 48.0f), &mutedBrush);
                }

                // Header separator line
                Gdiplus::Pen divPen(theme.divider, 1.0f);
                g.DrawLine(&divPen, 40.0f, 78.0f, 540.0f, 78.0f);

                // Page 1: PAGE_OPTIONS (Fresh Installation)
                if (g_currentPage == PAGE_OPTIONS) {
                    // Section 1: Destination Folder
                    g.DrawString(L"Installation Location", -1, g_pFontHeading, Gdiplus::PointF(40.0f, 98.0f), &headingBrush);
                    g.DrawString(L"The installer will copy AKS compiler and tools into this directory:", -1, g_pFontSub, Gdiplus::PointF(40.0f, 118.0f), &mutedBrush);

                    // Path Input Box Container (40 to 444, width 404, height 34)
                    Gdiplus::SolidBrush inputBg(theme.inputBg);
                    Gdiplus::Pen inputBorder(theme.inputBorder, 1.0f);
                    DrawRoundedRect(g, &inputBorder, &inputBg, 40.0f, 134.0f, 404.0f, 34.0f, 5.0f);

                    // Browse Button (454 to 540, width 86, height 34)
                    DrawModernButton(g, RC_BTN_BROWSE, L"Browse...", false, g_hoverBrowse, g_pressBrowse, theme, g_pFontBtn);

                    // Section 2: Installation Options
                    g.DrawString(L"Options", -1, g_pFontHeading, Gdiplus::PointF(40.0f, 184.0f), &headingBrush);

                    // Checkbox 1: PATH
                    DrawModernCheckbox(g, 40.0f, 212.0f, g_optPath, g_hoverRowPath, theme);
                    g.DrawString(L"Add AKS to User PATH (recommended)", -1, g_pFontBodyBold, Gdiplus::PointF(64.0f, 206.0f), &headingBrush);
                    g.DrawString(L"Enables 'aks' and 'aksls' commands directly from any terminal shell", -1, g_pFontSub, Gdiplus::PointF(64.0f, 224.0f), &mutedBrush);

                    // Checkbox 2: Association
                    DrawModernCheckbox(g, 40.0f, 258.0f, g_optAssoc, g_hoverRowAssoc, theme);
                    g.DrawString(L"Associate .aks source files", -1, g_pFontBodyBold, Gdiplus::PointF(64.0f, 252.0f), &headingBrush);
                    g.DrawString(L"Registers file type icon and enables direct execution of scripts", -1, g_pFontSub, Gdiplus::PointF(64.0f, 270.0f), &mutedBrush);

                    // Checkbox 3: VS Code
                    DrawModernCheckbox(g, 40.0f, 304.0f, g_optVSCode, g_hoverRowVSCode, theme);
                    g.DrawString(L"Install Visual Studio Code extension", -1, g_pFontBodyBold, Gdiplus::PointF(64.0f, 298.0f), &headingBrush);
                    g.DrawString(L"Syntax highlighting, code formatting, and Language Server daemon", -1, g_pFontSub, Gdiplus::PointF(64.0f, 316.0f), &mutedBrush);

                    // Bottom Action Bar Separator (y: 368)
                    g.DrawLine(&divPen, 0.0f, 368.0f, 580.0f, 368.0f);

                    // Space note
                    g.DrawString(L"Space required: ~28 MB", -1, g_pFontSub, Gdiplus::PointF(40.0f, 388.0f), &mutedBrush);

                    // Bottom Action Buttons
                    DrawModernButton(g, RC_BTN_CANCEL, L"Cancel", false, g_hoverCancel, g_pressCancel, theme, g_pFontBtn);
                    DrawModernButton(g, RC_BTN_INSTALL, L"Install Now", true, g_hoverInstall, g_pressInstall, theme, g_pFontBtn);
                }
                // Page: PAGE_MAINTENANCE (Modify / Repair / Uninstall)
                else if (g_currentPage == PAGE_MAINTENANCE) {
                    // 1. Detected Location Info Card
                    Gdiplus::SolidBrush cardBg(theme.cardBg);
                    Gdiplus::Pen cardBorder(theme.cardBorder, 1.0f);
                    DrawRoundedRect(g, &cardBorder, &cardBg, 40.0f, 96.0f, 500.0f, 48.0f, 6.0f);

                    g.DrawString(L"Current Installation Directory:", -1, g_pFontSub, Gdiplus::PointF(54.0f, 103.0f), &mutedBrush);

                    wchar_t wDir[MAX_PATH];
                    MultiByteToWideChar(CP_UTF8, 0, g_targetDir, -1, wDir, MAX_PATH);
                    g.DrawString(wDir, -1, g_pFontBodyBold, Gdiplus::PointF(54.0f, 120.0f), &headingBrush);

                    // 2. Action Card 1: Repair (Matches RC_CARD_REPAIR = {40, 156, 540, 230})
                    Gdiplus::SolidBrush repairCardBg(g_hoverRepair ? theme.btnSecondaryHover : theme.cardBg);
                    Gdiplus::Pen repairBorder(g_hoverRepair ? theme.btnPrimaryBg : theme.cardBorder, g_hoverRepair ? 1.5f : 1.0f);
                    DrawRoundedRect(g, &repairBorder, &repairCardBg, 40.0f, 156.0f, 500.0f, 74.0f, 6.0f);

                    g.DrawString(L"Repair", -1, g_pFontHeading, Gdiplus::PointF(64.0f, 170.0f), &headingBrush);
                    g.DrawString(L"Reinstall all components, verify file integrity, and restore PATH settings", -1, g_pFontSub, Gdiplus::PointF(64.0f, 196.0f), &mutedBrush);

                    // Right Chevron
                    Gdiplus::SolidBrush repairChevBrush(g_hoverRepair ? theme.btnPrimaryBg : theme.textMuted);
                    g.DrawString(L"\x203A", -1, g_pFontChevron, Gdiplus::PointF(514.0f, 176.0f), &repairChevBrush);

                    // 3. Action Card 2: Uninstall (Matches RC_CARD_UNINSTALL = {40, 244, 540, 318})
                    Gdiplus::Color redCol(255, 239, 68, 68);
                    Gdiplus::SolidBrush uninstCardBg(g_hoverUninstall ? (g_isDarkMode ? Gdiplus::Color(255, 42, 26, 30) : Gdiplus::Color(255, 254, 242, 242)) : theme.cardBg);
                    Gdiplus::Pen uninstBorder(g_hoverUninstall ? redCol : theme.cardBorder, g_hoverUninstall ? 1.5f : 1.0f);
                    DrawRoundedRect(g, &uninstBorder, &uninstCardBg, 40.0f, 244.0f, 500.0f, 74.0f, 6.0f);

                    g.DrawString(L"Uninstall", -1, g_pFontHeading, Gdiplus::PointF(64.0f, 258.0f), &headingBrush);
                    g.DrawString(L"Completely remove AKS, toolchain binaries, and environment variables", -1, g_pFontSub, Gdiplus::PointF(64.0f, 284.0f), &mutedBrush);

                    // Right Chevron
                    Gdiplus::SolidBrush uninstChevBrush(g_hoverUninstall ? redCol : theme.textMuted);
                    g.DrawString(L"\x203A", -1, g_pFontChevron, Gdiplus::PointF(514.0f, 264.0f), &uninstChevBrush);

                    // Bottom separator
                    g.DrawLine(&divPen, 0.0f, 368.0f, 580.0f, 368.0f);

                    // Close Button
                    DrawModernButton(g, RC_BTN_CLOSE, L"Close", false, g_hoverClose, g_pressClose, theme, g_pFontBtn);
                }
                // Page 2: PAGE_INSTALLING
                else if (g_currentPage == PAGE_INSTALLING) {
                    wchar_t wStep[128];
                    MultiByteToWideChar(CP_UTF8, 0, g_installStepText, -1, wStep, 128);
                    g.DrawString(wStep, -1, g_pFontHeading, Gdiplus::PointF(40.0f, 140.0f), &headingBrush);

                    wchar_t wPct[32];
                    swprintf(wPct, 32, L"%d%%", g_installProgressPct);
                    Gdiplus::StringFormat sfRight;
                    sfRight.SetAlignment(Gdiplus::StringAlignmentFar);
                    g.DrawString(wPct, -1, g_pFontHeading, Gdiplus::RectF(40.0f, 140.0f, 500.0f, 24.0f), &sfRight, &headingBrush);

                    // Progress Track
                    Gdiplus::SolidBrush trackBrush(theme.progTrack);
                    DrawRoundedRect(g, NULL, &trackBrush, 40.0f, 174.0f, 500.0f, 8.0f, 4.0f);

                    // Progress Fill
                    float fillW = 500.0f * (float)g_installProgressPct / 100.0f;
                    if (fillW > 8.0f) {
                        Gdiplus::SolidBrush fillBrush(theme.progFill);
                        DrawRoundedRect(g, NULL, &fillBrush, 40.0f, 174.0f, fillW, 8.0f, 4.0f);
                    }

                    // Current file display
                    wchar_t wFile[MAX_PATH];
                    MultiByteToWideChar(CP_UTF8, 0, g_installCurrentFile, -1, wFile, MAX_PATH);
                    g.DrawString(wFile, -1, g_pFontMono, Gdiplus::PointF(40.0f, 196.0f), &mutedBrush);

                    // Bottom separator
                    g.DrawLine(&divPen, 0.0f, 368.0f, (float)width, 368.0f);

                    // Installing state button
                    DrawModernButton(g, RC_BTN_INSTALL, g_isRepairMode ? L"Repairing..." : L"Installing...", false, false, false, theme, g_pFontBtn);
                }
                // Page 3: PAGE_COMPLETED
                else if (g_currentPage == PAGE_COMPLETED) {
                    // Quick Start Card
                    Gdiplus::SolidBrush cardBg(theme.cardBg);
                    Gdiplus::Pen cardBorder(theme.cardBorder, 1.0f);
                    DrawRoundedRect(g, &cardBorder, &cardBg, 40.0f, 108.0f, 500.0f, 140.0f, 6.0f);

                    g.DrawString(L"Quick Start Commands:", -1, g_pFontHeading, Gdiplus::PointF(56.0f, 122.0f), &headingBrush);

                    Gdiplus::SolidBrush cmdBrush(theme.btnPrimaryBg);
                    g.DrawString(L"aks --version", -1, g_pFontMono, Gdiplus::PointF(56.0f, 150.0f), &cmdBrush);
                    g.DrawString(L"\x2014  Verify compiler and JIT runtime installation", -1, g_pFontSub, Gdiplus::PointF(170.0f, 150.0f), &mutedBrush);

                    g.DrawString(L"aks tour", -1, g_pFontMono, Gdiplus::PointF(56.0f, 176.0f), &cmdBrush);
                    g.DrawString(L"\x2014  Launch interactive hands-on terminal walkthrough", -1, g_pFontSub, Gdiplus::PointF(170.0f, 176.0f), &mutedBrush);

                    g.DrawString(L"aks run <file>", -1, g_pFontMono, Gdiplus::PointF(56.0f, 202.0f), &cmdBrush);
                    g.DrawString(L"\x2014  Compile and execute an AKS program", -1, g_pFontSub, Gdiplus::PointF(170.0f, 202.0f), &mutedBrush);

                    // Open Terminal Checkbox (RC_ROW_TERM = {40, 268, 540, 308})
                    DrawModernCheckbox(g, 40.0f, 276.0f, g_optOpenTerm, g_hoverRowTerm, theme);
                    g.DrawString(L"Open Command Prompt with AKS ready in PATH", -1, g_pFontBodyBold, Gdiplus::PointF(66.0f, 274.0f), &bodyBrush);

                    // Bottom separator
                    g.DrawLine(&divPen, 0.0f, 368.0f, (float)width, 368.0f);

                    DrawModernButton(g, RC_BTN_FINISH, L"Finish", true, g_hoverFinish, g_pressFinish, theme, g_pFontBtn);
                }
                // Page 4: PAGE_UNINSTALL
                else if (g_currentPage == PAGE_UNINSTALL) {
                    Gdiplus::SolidBrush cardBg(theme.cardBg);
                    Gdiplus::Pen cardBorder(theme.cardBorder, 1.0f);
                    DrawRoundedRect(g, &cardBorder, &cardBg, 40.0f, 108.0f, 500.0f, 140.0f, 6.0f);

                    g.DrawString(L"The following components will be cleanly removed:", -1, g_pFontHeading, Gdiplus::PointF(56.0f, 122.0f), &headingBrush);
                    g.DrawString(L"\x2022 All runtime binaries (aks.exe, aksls.exe, aksfmt.exe, akslint.exe)", -1, g_pFontSub, Gdiplus::PointF(56.0f, 150.0f), &bodyBrush);
                    g.DrawString(L"\x2022 User PATH environment variables and AKS_HOME", -1, g_pFontSub, Gdiplus::PointF(56.0f, 174.0f), &bodyBrush);
                    g.DrawString(L"\x2022 Windows file associations and uninstaller registration", -1, g_pFontSub, Gdiplus::PointF(56.0f, 198.0f), &bodyBrush);

                    // Bottom separator
                    g.DrawLine(&divPen, 0.0f, 368.0f, (float)width, 368.0f);

                    DrawModernButton(g, RC_BTN_CANCEL, L"Cancel", false, g_hoverCancel, g_pressCancel, theme, g_pFontBtn);
                    DrawModernButton(g, RC_BTN_INSTALL, L"Uninstall", true, g_hoverInstall, g_pressInstall, theme, g_pFontBtn);
                }
                // Page 5: PAGE_UNINSTALLING
                else if (g_currentPage == PAGE_UNINSTALLING) {
                    wchar_t wStep[128];
                    MultiByteToWideChar(CP_UTF8, 0, g_installStepText, -1, wStep, 128);
                    g.DrawString(wStep, -1, g_pFontHeading, Gdiplus::PointF(40.0f, 140.0f), &headingBrush);

                    Gdiplus::SolidBrush trackBrush(theme.progTrack);
                    DrawRoundedRect(g, NULL, &trackBrush, 40.0f, 174.0f, 500.0f, 8.0f, 4.0f);

                    float fillW = 500.0f * (float)g_installProgressPct / 100.0f;
                    if (fillW > 8.0f) {
                        Gdiplus::SolidBrush fillBrush(theme.progFill);
                        DrawRoundedRect(g, NULL, &fillBrush, 40.0f, 174.0f, fillW, 8.0f, 4.0f);
                    }

                    // Bottom separator
                    g.DrawLine(&divPen, 0.0f, 368.0f, (float)width, 368.0f);
                }
                // Page 6: PAGE_UNINSTALLED
                else if (g_currentPage == PAGE_UNINSTALLED) {
                    Gdiplus::SolidBrush cardBg(theme.cardBg);
                    Gdiplus::Pen cardBorder(theme.cardBorder, 1.0f);
                    DrawRoundedRect(g, &cardBorder, &cardBg, 40.0f, 108.0f, 500.0f, 80.0f, 6.0f);

                    g.DrawString(L"Uninstallation Completed", -1, g_pFontHeading, Gdiplus::PointF(56.0f, 122.0f), &headingBrush);
                    g.DrawString(L"AKS Programming Language has been cleanly removed from your computer.", -1, g_pFontSub, Gdiplus::PointF(56.0f, 146.0f), &mutedBrush);

                    // Bottom separator
                    g.DrawLine(&divPen, 0.0f, 368.0f, (float)width, 368.0f);

                    DrawModernButton(g, RC_BTN_FINISH, L"Finish", true, g_hoverFinish, g_pressFinish, theme, g_pFontBtn);
                }
            }

            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            break;
        }

        case WM_USER + 100: {
            ShowPage(PAGE_COMPLETED);
            break;
        }

        case WM_USER + 101: {
            ShowPage(PAGE_UNINSTALLED);
            break;
        }

        case WM_DESTROY: {
            if (g_pLogoBitmap) delete g_pLogoBitmap;
            if (g_pLogoFullDarkBitmap) delete g_pLogoFullDarkBitmap;
            if (g_pLogoFullLightBitmap) delete g_pLogoFullLightBitmap;
            if (g_pFontTitle) delete g_pFontTitle;
            if (g_pFontHeading) delete g_pFontHeading;
            if (g_pFontBody) delete g_pFontBody;
            if (g_pFontBodyBold) delete g_pFontBodyBold;
            if (g_pFontSub) delete g_pFontSub;
            if (g_pFontMono) delete g_pFontMono;
            if (g_pFontBtn) delete g_pFontBtn;
            if (g_pFontChevron) delete g_pFontChevron;
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hEditBrush) DeleteObject(g_hEditBrush);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)nCmdShow;
    InitDpiAwareness();
    g_dpiScale = DetectCurrentDpiScale(NULL);
    g_hInstance = hInstance;

    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    GetModuleFileNameA(NULL, g_selfPath, sizeof(g_selfPath));
    GetDefaultInstallDir(g_targetDir, sizeof(g_targetDir));

    // Parse command line
    if (strstr(g_selfPath, "uninstaller") != NULL) {
        g_isUninstallMode = true;
        char *bin_pos = strstr(g_selfPath, "\\bin\\");
        if (bin_pos) {
            size_t dlen = (size_t)(bin_pos - g_selfPath);
            strncpy(g_targetDir, g_selfPath, dlen);
            g_targetDir[dlen] = '\0';
        }
    }

    if (lpCmdLine && strlen(lpCmdLine) > 0) {
        if (strstr(lpCmdLine, "--silent") || strstr(lpCmdLine, "-s") || strstr(lpCmdLine, "/S") || strstr(lpCmdLine, "-y")) {
            g_isSilent = true;
        }
        if (strstr(lpCmdLine, "--uninstall") || strstr(lpCmdLine, "/U")) {
            g_isUninstallMode = true;
        }
        if (strstr(lpCmdLine, "--repair")) {
            g_isRepairMode = true;
        }
        if (strstr(lpCmdLine, "--modify") || strstr(lpCmdLine, "--maintenance")) {
            g_isAlreadyInstalled = true;
        }
        const char *dir_flag = strstr(lpCmdLine, "--dir");
        if (dir_flag) {
            dir_flag += 5;
            while (*dir_flag == ' ' || *dir_flag == '=') dir_flag++;
            if (*dir_flag == '"') {
                dir_flag++;
                const char *end_quote = strchr(dir_flag, '"');
                if (end_quote) {
                    size_t dlen = (size_t)(end_quote - dir_flag);
                    strncpy(g_targetDir, dir_flag, dlen);
                    g_targetDir[dlen] = '\0';
                }
            } else {
                size_t dlen = strcspn(dir_flag, " \t\r\n");
                strncpy(g_targetDir, dir_flag, dlen);
                g_targetDir[dlen] = '\0';
            }
        }
    }

    // Detect if AKS is already installed on the computer
    if (!g_isUninstallMode) {
        char detected_dir[MAX_PATH] = {0};
        if (CheckExistingInstallation(detected_dir, sizeof(detected_dir))) {
            g_isAlreadyInstalled = true;
            snprintf(g_installedDir, sizeof(g_installedDir), "%s", detected_dir);
            if (!strstr(lpCmdLine ? lpCmdLine : "", "--dir")) {
                snprintf(g_targetDir, sizeof(g_targetDir), "%s", detected_dir);
            }
        }
    }

    // Silent mode execution
    if (g_isSilent) {
        if (g_isUninstallMode) {
            char bin_dir[MAX_PATH];
            snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", g_targetDir);
            RemoveFromUserPath(bin_dir);
            RemoveAksHome();
            UnregisterFileAssociation();
            UnregisterUninstallEntry();
            DeleteDirectoryRecursively(g_targetDir);
            char cmd[MAX_PATH * 3];
            snprintf(cmd, sizeof(cmd), "/c ping 127.0.0.1 -n 2 >nul & rmdir /s /q \"%s\"", g_targetDir);
            ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
            Gdiplus::GdiplusShutdown(gdiplusToken);
            return 0;
        } else {
            EnsureDirectory(g_targetDir);
            char bin_dir[MAX_PATH];
            snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", g_targetDir);
            EnsureDirectory(bin_dir);

            ExtractDistribution(g_selfPath, g_targetDir);

            char uninst_path[MAX_PATH];
            snprintf(uninst_path, sizeof(uninst_path), "%s\\bin\\aks-uninstaller.exe", g_targetDir);
            CopyFileA(g_selfPath, uninst_path, FALSE);

            char aks_exe[MAX_PATH];
            snprintf(aks_exe, sizeof(aks_exe), "%s\\bin\\aks.exe", g_targetDir);

            AddToUserPath(bin_dir);
            SetAksHome(g_targetDir);
            RegisterFileAssociation(aks_exe);
            RegisterUninstallEntry(g_targetDir, uninst_path, aks_exe);

            char vsix_path[MAX_PATH];
            snprintf(vsix_path, sizeof(vsix_path), "%s\\editors\\vscode\\aks-lang-1.0.0.vsix", g_targetDir);
            if (_access(vsix_path, 0) == 0) {
                char code_cmd[MAX_PATH * 2];
                snprintf(code_cmd, sizeof(code_cmd), "code --install-extension \"%s\" --force", vsix_path);
                RunProcessSilently(code_cmd);
            }

            Gdiplus::GdiplusShutdown(gdiplusToken);
            return 0;
        }
    }

    // Register Window Class
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "AksMinimalistInstallerWindowClass";

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Failed to register installer window class.", "Error", MB_ICONERROR);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    RECT wr = {0, 0, (int)(580.0f * g_dpiScale), (int)(440.0f * g_dpiScale)};
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    int posX = (GetSystemMetrics(SM_CXSCREEN) - (wr.right - wr.left)) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - (wr.bottom - wr.top)) / 2;

    const char *win_title = g_isUninstallMode ? "Uninstall - AKS Programming Language" :
                           (g_isAlreadyInstalled ? "Modify Installation - AKS Programming Language" : "AKS Programming Language Installer");
    g_hMainWnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        win_title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hMainWnd) {
        MessageBoxA(NULL, "Failed to create installer window.", "Error", MB_ICONERROR);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    ShowWindow(g_hMainWnd, SW_SHOW);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
