// What the dashboard needs from the operating system beyond drawing: the
// window it lives in, a millisecond clock, and the odd system action. Windows
// has platform_win.cpp; Linux has linux/platform_linux.cpp.
#pragma once

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#else
#include <climits>
struct SDL_Window;
// As Windows' INFINITE: no timeout.
#ifndef INFINITE
#define INFINITE 0xFFFFFFFFUL
#endif
#endif

namespace ks {

#ifdef _WIN32
using NativeWindow = HWND;
using GpuDevice = ID3D11Device*;
#else
using NativeWindow = SDL_Window*;
using GpuDevice = void*;  // OpenGL needs no device: textures go to the current context
#endif

// TickMs is a monotonic clock in milliseconds, as GetTickCount64.
uint64_t TickMs();

// The window's state and its title-bar buttons.
bool WindowMaximised(NativeWindow w);
bool WindowActive(NativeWindow w);
void MinimiseWindow(NativeWindow w);
void ToggleMaximise(NativeWindow w);
// CloseMainWindow asks the window to close, as its close button would.
void CloseMainWindow(NativeWindow w);
// MouseOverWindow reports whether the pointer is over the window, not over
// another window in front of it.
bool MouseOverWindow(NativeWindow w);

// DisplaysOff turns the displays off after a short wait (letting go of the
// mouse would wake them at once). It returns at once.
void DisplaysOff();
// CanTurnDisplaysOff says whether DisplaysOff can work here.
bool CanTurnDisplaysOff();

}  // namespace ks
