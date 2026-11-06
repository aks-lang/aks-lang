// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef CROSSH
#define CROSSH

#include "value.h"

// clipboard APIs
void SysClipSetText(const char *text);
char *SysClipGetText(void);

// notification & System Tray APIs
void SysNotifyShow(const char *title, const char *message);
void SysTrayAdd(const char *iconpath, const char *tooltip);
void SysTrayRemove(void);

// Gui Windowing & Controls (Win32 wrapper)
AKSValue SysGUINewWin(const char *title, int width, int height);
AKSValue SysGUINewBtn(AKSValue parentwin, const char *text, int x, int y, int w, int h);
void SysGUIMsgBox(const char *title, const char *message);
int SysGUIMsgLoop(void);

// Sdl / Raylib Graphics wrappers
int SysSDLInit(void);
AKSValue SysSDLNewWin(const char *title, int w, int h);
void SysSDLClear(int r, int g, int b);
void SysSDLDrawRect(int x, int y, int w, int h, int r, int g, int b);
void SysSDLDrawCircle(int x, int y, int radius, int r, int g, int b);
int SysSDLPollEvent(void);

// Browser dom emulation
AKSValue SysDOMGetElementByID(const char *id);
void SysDOMSetInnerHtml(AKSValue element, const char *html);

// drag and drop hook
void SysGUIEnableDragDrop(AKSValue window);
char *SysGUIGetDroppedFiles(void);

// webAssembly compilation & interpretation
int CompileToWasm(const char *sourcepath, const char *outputpath);
int RunWasmBinary(const char *wasmpath);

// Enable ANSI escape sequence processing on Windows
void SysEnableAnsi(void);

// Cross-compilation target descriptor
typedef struct {
    const char *triple;        // LLVM/GCC target triple
    const char *alias;         // Short friendly alias
    const char *os;            // "linux", "macos", "windows", "none"
    const char *arch;          // "x86_64", "aarch64", "riscv64", "wasm32", etc.
    const char *abi;           // "gnu", "musl", "msvc", "eabi"
    int pointer_size;          // Pointer size in bytes (4 or 8)
    int big_endian;            // 1 = big-endian, 0 = little-endian
    const char *cc_flags;      // Extra C compiler flags for this target
    const char *linker_flags;  // Extra linker flags for this target
} CrossTarget;

// Return the full table of supported cross-compilation targets (NULL-terminated)
const CrossTarget *CrossGetTargets(void);

// Look up a target by triple or alias; returns NULL if not found
const CrossTarget *CrossFindTarget(const char *name);

// Print a formatted table of all supported targets to stdout
void CrossListTargets(void);

// Resolve a friendly alias or partial triple to the canonical triple string.
// Returns name unchanged if no alias matches.
const char *CrossResolveTriple(const char *name);

// Generate a cross-compilation command-line flag string for the given target.
// outbuf must be at least 256 bytes.
void CrossBuildFlags(const CrossTarget *tgt, char *outbuf, int bufsize);

#endif // Crossh
