// platform.h for Linux, on SDL3.
#include <SDL3/SDL.h>

#include "platform.h"

namespace ks {

uint64_t TickMs() { return SDL_GetTicks(); }

bool WindowMaximised(NativeWindow w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) != 0; }
bool WindowActive(NativeWindow w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) != 0; }
void MinimiseWindow(NativeWindow w) { SDL_MinimizeWindow(w); }

void ToggleMaximise(NativeWindow w) {
    if (WindowMaximised(w))
        SDL_RestoreWindow(w);
    else
        SDL_MaximizeWindow(w);
}

void CloseMainWindow(NativeWindow w) {
    SDL_Event e{};
    e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    e.window.windowID = SDL_GetWindowID(w);
    SDL_PushEvent(&e);
}

bool MouseOverWindow(NativeWindow w) { return SDL_GetMouseFocus() == w; }

// The displays are the laptop's own business here.
void DisplaysOff() {}
bool CanTurnDisplaysOff() { return false; }

}  // namespace ks
