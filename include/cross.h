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

#endif // Crossh
