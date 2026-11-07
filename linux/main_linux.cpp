// kitchensink's dashboard on Linux: the same UI as the Windows client, in an
// SDL3 window drawn with OpenGL. No tray: closing the window quits.
// Notifications go through notify-send.
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "app.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

namespace {

SDL_Window* window = nullptr;
std::unique_ptr<ks::App> app;
Uint32 wakeEvent = 0;
int framesWanted = 2;

// density is device pixels per window coordinate (1.25 at 125% scaling on
// Wayland). The dashboard works in device pixels, as on Windows, so text is
// drawn sharp at the screen's own resolution.
float density() {
    float d = SDL_GetWindowPixelDensity(window);
    return d > 0 ? d : 1.0f;
}

// toPixels turns an event's window coordinates into device pixels before
// ImGui sees it.
void toPixels(SDL_Event& e) {
    float d = density();
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        e.motion.x *= d;
        e.motion.y *= d;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        e.button.x *= d;
        e.button.y *= d;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        e.wheel.mouse_x *= d;
        e.wheel.mouse_y *= d;
        break;
    }
}

// hitTest finds what part of the window a point is over. There's no frame,
// so the resize borders are a band inside the window's own edges, and the
// dashboard's header is the title bar.
SDL_HitTestResult SDLCALL hitTest(SDL_Window* w, const SDL_Point* pt, void*) {
    int width = 0, height = 0;
    SDL_GetWindowSize(w, &width, &height);
    if (!(SDL_GetWindowFlags(w) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))) {
        const int band = 6, corner = 14;
        bool left = pt->x < band, right = pt->x >= width - band;
        bool top = pt->y < band, bottom = pt->y >= height - band;
        bool nearLeft = pt->x < corner, nearRight = pt->x >= width - corner;
        bool nearTop = pt->y < corner, nearBottom = pt->y >= height - corner;
        if ((top && nearLeft) || (left && nearTop)) return SDL_HITTEST_RESIZE_TOPLEFT;
        if ((top && nearRight) || (right && nearTop)) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if ((bottom && nearLeft) || (left && nearBottom)) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if ((bottom && nearRight) || (right && nearBottom)) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
    }
    float d = density();
    if (app && app->InCaption(pt->x * d, pt->y * d)) return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

// showNotification hands a notification to the desktop through notify-send,
// without waiting for it.
void showNotification(const std::string& title, const std::string& body) {
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            int null = open("/dev/null", O_RDWR);
            if (null >= 0) {
                dup2(null, 1);
                dup2(null, 2);
            }
            execlp("notify-send", "notify-send", "--app-name=kitchensink", "--icon=kitchensink", "--",
                   title.c_str(), body.c_str(), (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, nullptr, 0);
}

void (*glViewportFn)(GLint, GLint, GLsizei, GLsizei) = nullptr;
void (*glClearColorFn)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
void (*glClearFn)(GLbitfield) = nullptr;

// renderFrame draws one frame. Without present, it only lays out, so sizes
// measured this frame are right in the next.
void renderFrame(bool present) {
    if (app->FontsDirty()) {
        app->ReloadFonts();
        ImGui_ImplOpenGL3_DestroyDeviceObjects();  // remade, with the new fonts, by NewFrame
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    // Device pixels throughout (see density).
    ImGuiIO& io = ImGui::GetIO();
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    io.DisplaySize = ImVec2(float(pw), float(ph));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    ImGui::NewFrame();
    app->Frame();
    ImGui::Render();
    app->PostRender(ImGui::GetDrawData());
    if (present) {
        const ImVec4& bg = ks::Colours().bg;
        glViewportFn(0, 0, pw, ph);
        glClearColorFn(bg.x, bg.y, bg.z, 1);
        glClearFn(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
}

// oneInstance holds a lock for as long as the app runs; false if another
// copy has it.
bool oneInstance() {
    const char* dir = std::getenv("XDG_RUNTIME_DIR");
    std::string path = std::string(dir && *dir ? dir : "/tmp") + "/kitchensink.lock";
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0;  // fd stays open until exit
}

}  // namespace

int main(int argc, char** argv) {
    bool startMinimised = false;
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--minimised") == 0 || std::strcmp(argv[i], "--tray") == 0) startMinimised = true;
    if (!oneInstance()) {
        std::fprintf(stderr, "kitchensink is already running\n");
        return 0;
    }

    // Wayland's app ID, which matches the .desktop file for the icon.
    SDL_SetHint(SDL_HINT_APP_ID, "kitchensink");
    SDL_SetAppMetadata("kitchensink", "1.0", "kitchensink");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "couldn't start SDL: %s\n", SDL_GetError());
        return 1;
    }
    wakeEvent = SDL_RegisterEvents(1);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // 1360x860 like the Windows client, but never bigger than the screen.
    int w = 1360, h = 860;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)) {
        w = std::min(w, usable.w * 9 / 10);
        h = std::min(h, usable.h * 9 / 10);
    }
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS |
                            SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    window = SDL_CreateWindow("kitchensink", w, h, flags);
    if (!window) {
        std::fprintf(stderr, "couldn't open a window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 480, 360);
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "couldn't start OpenGL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);
    glViewportFn = (decltype(glViewportFn))SDL_GL_GetProcAddress("glViewport");
    glClearColorFn = (decltype(glClearColorFn))SDL_GL_GetProcAddress("glClearColor");
    glClearFn = (decltype(glClearFn))SDL_GL_GetProcAddress("glClear");
    SDL_SetWindowHitTest(window, hitTest, nullptr);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    app = std::make_unique<ks::App>(
        window, nullptr,
        [] {
            SDL_Event e{};
            e.type = wakeEvent;
            SDL_PushEvent(&e);
        },
        showNotification);
    app->SetScale(SDL_GetWindowDisplayScale(window));
    app->SetDark(ks::SystemUsesDarkTheme());
    SDL_ShowWindow(window);
    if (startMinimised) SDL_MinimizeWindow(window);

    // Draw only when something happens: input, data from the server, or
    // once a second so "updated 3m ago" stays current; more often while the
    // cat's moving.
    bool running = true;
    while (running) {
        bool visible = !(SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
        unsigned long timeout = !visible                               ? 1000
                                : (framesWanted > 0 || app->Busy()) ? 0
                                : app->FrameInterval() > 0          ? app->FrameInterval()
                                                                    : 1000;
        timeout = std::min<unsigned long>(timeout, app->FlushIn());
        SDL_Event e;
        bool got = timeout == 0 ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, Sint32(std::min(timeout, 60000UL)));
        while (got) {
            if (e.type != wakeEvent) {
                toPixels(e);
                ImGui_ImplSDL3_ProcessEvent(&e);
                framesWanted = 2;  // ImGui needs a couple of frames to settle after input
            }
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (e.window.windowID == SDL_GetWindowID(window)) running = false;
                break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                app->SetScale(SDL_GetWindowDisplayScale(window));
                ImGui_ImplOpenGL3_DestroyDeviceObjects();
                break;
            case SDL_EVENT_SYSTEM_THEME_CHANGED: app->SetDark(ks::SystemUsesDarkTheme()); break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                // Lay out once at the new size first, so it's measured.
                renderFrame(false);
                break;
            }
            got = SDL_PollEvent(&e);
        }
        if (!running) break;
        app->Pump();
        if (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) continue;
        renderFrame(true);
        if (framesWanted > 0) framesWanted--;
    }

    app.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
