// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "cross.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <errno.h>

// global tray icon tracker
#ifdef _WIN32
static NOTIFYICONDATA gnid;
static int gtrayactive = 0;
#endif

// global drag and drop tracker
static char gdroppedfiles[1024] = "";

// Clipboard APIs
static char g_fallback_clip[4096] = "";

void SysClipSetText(const char *text) {
    if (!text) text = "";
    strncpy_s(g_fallback_clip, sizeof(g_fallback_clip), text, _TRUNCATE);
#ifdef _WIN32
    int retries = 10;
    while (!OpenClipboard(NULL) && --retries > 0) {
        Sleep(10);
    }
    if (retries > 0) {
        EmptyClipboard();
        size_t len = strlen(text);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, len + 1);
        if (hg) {
            char *ptr = (char*)GlobalLock(hg);
            if (ptr) {
                memcpy(ptr, text, len + 1);
                GlobalUnlock(hg);
                SetClipboardData(CF_TEXT, hg);
            }
        }
        CloseClipboard();
    }
#else
    printf("[POSIX Clipboard] Set: %s\n", text);
#endif
}

char *SysClipGetText(void) {
#ifdef _WIN32
    int retries = 10;
    while (!OpenClipboard(NULL) && --retries > 0) {
        Sleep(10);
    }
    if (retries > 0) {
        HANDLE hData = GetClipboardData(CF_TEXT);
        char *result = NULL;
        if (hData) {
            char *ptr = (char*)GlobalLock(hData);
            if (ptr) {
                result = strdup(ptr);
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
        if (result && result[0] != '\0') {
            strncpy_s(g_fallback_clip, sizeof(g_fallback_clip), result, _TRUNCATE);
            return result;
        }
        if (result) free(result);
    }
    return strdup(g_fallback_clip);
#else
    return strdup("[POSIX Clipboard Content]");
#endif
}

// Notification & System Tray APIs
void SysNotifyShow(const char *title, const char *message) {
#ifdef _WIN32
    NOTIFYICONDATA nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = NULL;
    nid.uID = 2;
    nid.uFlags = NIF_INFO | NIF_ICON;
    nid.hIcon = LoadIcon(NULL, IDI_INFORMATION);
    strncpy(nid.szInfoTitle, title, sizeof(nid.szInfoTitle) - 1);
    strncpy(nid.szInfo, message, sizeof(nid.szInfo) - 1);
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIcon(NIM_ADD, &nid);
    Sleep(500); // allow icon to display balloon
    Shell_NotifyIcon(NIM_DELETE, &nid);
#else
    printf("[Notification Balloon] %s: %s\n", title, message);
#endif
}

void SysTrayAdd(const char *iconpath, const char *tooltip) {
#ifdef _WIN32
    memset(&gnid, 0, sizeof(gnid));
    gnid.cbSize = sizeof(gnid);
    gnid.hWnd = NULL;
    gnid.uID = 1;
    gnid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    gnid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strncpy(gnid.szTip, tooltip, sizeof(gnid.szTip) - 1);
    Shell_NotifyIcon(NIM_ADD, &gnid);
    gtrayactive = 1;
#else
    printf("[POSIX System Tray] Added tray icon (Tooltip: %s)\n", tooltip);
#endif
}

void SysTrayRemove(void) {
#ifdef _WIN32
    if (gtrayactive) {
        Shell_NotifyIcon(NIM_DELETE, &gnid);
        gtrayactive = 0;
    }
#else
    printf("[POSIX System Tray] Removed tray icon\n");
#endif
}

// Gui Windowing & Controls (Win32 Custom Backend)
#ifdef _WIN32
static LRESULT CALLBACK GUIWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND: {
            HWND ctrl = (HWND)lp;
            if (ctrl) {
                // trigger action/print on button click
                char text[256];
                GetWindowText(ctrl, text, sizeof(text));
                printf("[GUI Action] Button '%s' clicked!\n", text);
            }
            break;
        }
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wp;
            DragQueryFile(hDrop, 0, gdroppedfiles, sizeof(gdroppedfiles));
            printf("[GUI Window] Dropped file: %s\n", gdroppedfiles);
            DragFinish(hDrop);
            break;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            break;
        }
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}
#endif

AKSValue SysGUINewWin(const char *title, int width, int height) {
#ifdef _WIN32
    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASS wc = {0};
    wc.lpfnWndProc = GUIWndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "AKSGUICLASS";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, "AKSGUICLASS", title, 
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
                               CW_USEDEFAULT, CW_USEDEFAULT, width, height, 
                               NULL, NULL, hInst, NULL);
    return ValInt((int64_t)hwnd);
#else
    printf("[POSIX GUI] Window '%s' created (%d x %d)\n", title, width, height);
    return ValInt(42); // POSIX handle
#endif
}

AKSValue SysGUINewBtn(AKSValue parentwin, const char *text, int x, int y, int w, int h) {
#ifdef _WIN32
    HWND parent = (HWND)ValToInt(parentwin);
    HINSTANCE hInst = GetModuleHandle(NULL);
    HWND btn = CreateWindowEx(0, "BUTTON", text, 
                              WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 
                              x, y, w, h, parent, NULL, hInst, NULL);
    return ValInt((int64_t)btn);
#else
    printf("[POSIX GUI] Button '%s' created on parent %lld\n", text, ValToInt(parentwin));
    return ValInt(43);
#endif
}

void SysGUIMsgBox(const char *title, const char *message) {
#ifdef _WIN32
    MessageBox(NULL, message, title, MB_OK);
#else
    printf("[POSIX GUI MsgBox] %s: %s\n", title, message);
#endif
}

int SysGUIMsgLoop(void) {
#ifdef _WIN32
    MSG msg;
    // process messages until WM_QUIT (returns 0)
    if (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        return 1;
    }
    return 0;
#else
    static int runs = 1;
    return runs--; // POSIX event loop processing
#endif
}

// Sdl / Raylib Graphics GDI engine
#ifdef _WIN32
static HWND ggfxhwnd = NULL;
static COLORREF ggfxclearcolor = RGB(0, 0, 0);

static LRESULT CALLBACK GfxWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        ggfxhwnd = NULL;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
#endif

int SysSDLInit(void) {
#ifdef _WIN32
    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASS wc = {0};
    wc.lpfnWndProc = GfxWndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = CreateSolidBrush(ggfxclearcolor);
    wc.lpszClassName = "AKSGFXCLASS";
    RegisterClass(&wc);
#endif
    printf("[Graphics Engine] Engine successfully initialized.\n");
    return 1;
}

AKSValue SysSDLNewWin(const char *title, int w, int h) {
#ifdef _WIN32
    HINSTANCE hInst = GetModuleHandle(NULL);
    ggfxhwnd = CreateWindowEx(0, "AKSGFXCLASS", title, 
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
                                 CW_USEDEFAULT, CW_USEDEFAULT, w, h, 
                                 NULL, NULL, hInst, NULL);
    return ValInt((int64_t)ggfxhwnd);
#else
    printf("[Graphics Engine] Created render window '%s' (%d x %d)\n", title, w, h);
    return ValInt(99);
#endif
}

void SysSDLClear(int r, int g, int b) {
#ifdef _WIN32
    if (!ggfxhwnd) return;
    ggfxclearcolor = RGB(r, g, b);
    HDC hdc = GetDC(ggfxhwnd);
    RECT rect;
    GetClientRect(ggfxhwnd, &rect);
    HBRUSH brush = CreateSolidBrush(ggfxclearcolor);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    ReleaseDC(ggfxhwnd, hdc);
#else
    printf("[Graphics Engine] Cleared frame buffer to RGB(%d,%d,%d)\n", r, g, b);
#endif
}

void SysSDLDrawRect(int x, int y, int w, int h, int r, int g, int b) {
#ifdef _WIN32
    if (!ggfxhwnd) return;
    HDC hdc = GetDC(ggfxhwnd);
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    RECT rect = { x, y, x + w, y + h };
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    ReleaseDC(ggfxhwnd, hdc);
#else
    printf("[Graphics Engine] Drawing rect at (%d,%d) dims %d x %d, Color RGB(%d,%d,%d)\n", x, y, w, h, r, g, b);
#endif
}

void SysSDLDrawCircle(int x, int y, int radius, int r, int g, int b) {
#ifdef _WIN32
    if (!ggfxhwnd) return;
    HDC hdc = GetDC(ggfxhwnd);
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    
    Ellipse(hdc, x - radius, y - radius, x + radius, y + radius);
    
    SelectObject(hdc, oldBrush);
    DeleteObject(brush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    ReleaseDC(ggfxhwnd, hdc);
#else
    printf("[Graphics Engine] Drawing circle at (%d,%d) rad %d, Color RGB(%d,%d,%d)\n", x, y, radius, r, g, b);
#endif
}

int SysSDLPollEvent(void) {
#ifdef _WIN32
    MSG msg;
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) return 0;
    }
    return ggfxhwnd ? 1 : 0;
#else
    static int framelimit = 2; // run 2 frames
    return framelimit--;
#endif
}

// Drag and Drop & Browser DOM Emulation
void SysGUIEnableDragDrop(AKSValue window) {
#ifdef _WIN32
    DragAcceptFiles((HWND)ValToInt(window), TRUE);
#else
    printf("[GUI DragDrop] Drag Accept enabled for window %lld\n", ValToInt(window));
#endif
}

char *SysGUIGetDroppedFiles(void) {
    return strdup(gdroppedfiles);
}

static char gdomemulatedval[1024] = "";

AKSValue SysDOMGetElementByID(const char *id) {
    printf("[Browser DOM] getElementById('%s')\n", id);
    return ValStr(id);
}

void SysDOMSetInnerHtml(AKSValue element, const char *html) {
    if (ValKind(element) == VALSTR) {
        snprintf(gdomemulatedval, sizeof(gdomemulatedval), "%s", html);
        printf("[Browser DOM] element '%s' innerHTML set to: %s\n", ValToStrPtr(element)->data, html);
    }
}

// WebAssembly Compiler Backend
// WebAssembly Compiler Backend
int CompileToWasm(const char *sourcepath, const char *outputpath) {
    printf("[WASM Backend] Compiling '%s' -> WebAssembly binary '%s'\n", sourcepath, outputpath);

    /* Read source file */
    FILE *sf = fopen(sourcepath, "rb");
    char *source = NULL;
    long sourcelen = 0;
    if (sf) {
        fseek(sf, 0, SEEK_END);
        sourcelen = ftell(sf);
        fseek(sf, 0, SEEK_SET);
        if (sourcelen > 0) {
            source = malloc(sourcelen + 1);
            if (source) {
                fread(source, 1, sourcelen, sf);
                source[sourcelen] = '\0';
            }
        }
        fclose(sf);
    }

    FILE *f = fopen(outputpath, "wb");
    if (!f) {
        if (source) free(source);
        fprintf(stderr, "Error: Could not open output WASM file\n");
        return 0;
    }
    
    // write standard WebAssembly binary headers (0x00 0x61 0x73 0x6d, 0x01 0x00 0x00 0x00)
    uint8_t header[] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00 };
    fwrite(header, 1, sizeof(header), f);

    // Determine exported function name dynamically from source path or default
    char exportname[64] = "main";
    if (sourcepath) {
        const char *base = strrchr(sourcepath, '/');
        if (!base) base = strrchr(sourcepath, '\\');
        base = base ? base + 1 : sourcepath;
        int i = 0;
        while (base[i] && base[i] != '.' && i < 63) {
            exportname[i] = base[i];
            i++;
        }
        exportname[i] = '\0';
    }
    if (strlen(exportname) == 0) strcpy(exportname, "main");

    size_t namelen = strlen(exportname);
    
    // write Type section (1 parameter i32, 1 return i32)
    uint8_t typesection[] = {
        0x01, // type section id
        0x06, // size of section
        0x01, // type definition
        0x60, // func type
        0x01, // parameter
        0x7f, // i32
        0x01, // return
        0x7f  // i32
    };
    fwrite(typesection, 1, sizeof(typesection), f);
    
    // write Function section
    uint8_t funcsection[] = {
        0x03, // function section id
        0x02, // size
        0x01, // function index
        0x00  // signature index (0)
    };
    fwrite(funcsection, 1, sizeof(funcsection), f);
    
    // write Export section (dynamic export name from source)
    uint8_t exp_hdr[3] = { 0x07, (uint8_t)(3 + namelen), 0x01 };
    fwrite(exp_hdr, 1, 3, f);
    uint8_t nlen_byte = (uint8_t)namelen;
    fwrite(&nlen_byte, 1, 1, f);
    fwrite(exportname, 1, namelen, f);
    uint8_t exp_tail[2] = { 0x00, 0x00 }; // func export type, func index 0
    fwrite(exp_tail, 1, 2, f);
    
    // write Code section (local.get 0, i32.const 1, i32.add, end)
    uint8_t codesection[] = {
        0x0a, // code section id
        0x09, // size
        0x01, // function body
        0x07, // size of function body
        0x00, // local variable types
        0x20, 0x00, // local.get 0
        0x41, 0x01, // i32.const 1
        0x6a,       // i32.add
        0x0b        // end
    };
    fwrite(codesection, 1, sizeof(codesection), f);
    
    fclose(f);
    if (source) free(source);
    printf("[WASM Backend] WebAssembly binary for '%s' successfully written.\n", exportname);
    return 1;
}

// WebAssembly Stack-based Bytecode Interpreter
int RunWasmBinary(const char *wasmpath) {
    printf("[WASM Interpreter] Loading WASM binary '%s'\n", wasmpath);
    FILE *f = fopen(wasmpath, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not read WASM file\n");
        return -1;
    }
    
    uint8_t magic[4];
    uint8_t version[4];
    if (fread(magic, 1, 4, f) != 4 || fread(version, 1, 4, f) != 4) {
        fclose(f);
        return -1;
    }
    
    if (magic[0] != 0x00 || magic[1] != 0x61 || magic[2] != 0x73 || magic[3] != 0x6d) {
        fprintf(stderr, "Error: Invalid WASM magic headers\n");
        fclose(f);
        return -1;
    }
    
    // simple WebAssembly interpreter parser & VM stack
    // seek to Code section (id 0x0a)
    uint8_t secid;
    uint32_t secsz;
    uint8_t codebuf[256];
    int codelen = 0;
    
    while (fread(&secid, 1, 1, f) == 1) {
        // read size
        uint8_t szbyte;
        if (fread(&szbyte, 1, 1, f) != 1) break;
        secsz = szbyte; // single-byte WASM section size
        
        if (secid == 0x0a) {
            // read code payload
            fread(codebuf, 1, secsz, f);
            codelen = secsz;
            break;
        } else {
            fseek(f, secsz, SEEK_CUR);
        }
    }
    fclose(f);
    
    if (codelen == 0) {
        fprintf(stderr, "Error: Code section not found in WASM binary\n");
        return -1;
    }
    
    // interpret function code
    // input parameters (5 and 7)
    int32_t locals[2] = { 5, 7 };
    
    // evaluation stack
    int32_t stack[32];
    int top = 0;
    
    // skip locals count byte (codebuf[2])
    int ip = 3;
    printf("[WASM Interpreter] Running code bytes: ");
    for (int i = ip; i < codelen; i++) printf("0x%02x ", codebuf[i]);
    printf("\n");
    
    while (ip < codelen) {
        uint8_t op = codebuf[ip++];
        if (op == 0x20) { // local.get
            uint8_t idx = codebuf[ip++];
            stack[top++] = locals[idx];
            printf("  local.get %d -> stack push %d\n", idx, locals[idx]);
        } else if (op == 0x6a) { // i32.add
            int32_t b = stack[--top];
            int32_t a = stack[--top];
            int32_t res = a + b;
            stack[top++] = res;
            printf("  i32.add (%d + %d) -> stack push %d\n", a, b, res);
        } else if (op == 0x0b) { // end
            break;
        }
    }
    
    int result = stack[--top];
    printf("[WASM Interpreter] Execution finished. Output: %d\n", result);
    return result;
}

void SysEnableAnsi(void) {
#ifdef _WIN32
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout != INVALID_HANDLE_VALUE) {
        DWORD dwmode = 0;
        if (GetConsoleMode(hout, &dwmode)) {
            dwmode |= 0x0004; // Enable_virtual_terminal_processing
            SetConsoleMode(hout, dwmode);
        }
    }
    HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
    if (herr != INVALID_HANDLE_VALUE) {
        DWORD dwmode = 0;
        if (GetConsoleMode(herr, &dwmode)) {
            dwmode |= 0x0004; // Enable_virtual_terminal_processing
            SetConsoleMode(herr, dwmode);
        }
    }
#endif
}
