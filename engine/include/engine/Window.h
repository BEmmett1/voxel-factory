#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace engine {

    // Owns the SDL window + OpenGL context. Creating one initializes SDL video,
    // makes a 3.3-core context current, and loads the GL function pointers via
    // glad. Throws std::runtime_error if any of that fails.
    class Window {
    public:
        Window(const std::string& title, int width, int height);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        void swap();              // present the back buffer
        void updateViewport();    // glViewport to the current drawable size

        int width() const;        // drawable size in pixels
        int height() const;
        float aspect() const;

        // Captured (FPS look) or released (panels). Either way the OS cursor
        // stays hidden over the window; a released mouse is drawn by the game.
        void setRelativeMouse(bool enabled);
        bool relativeMouse() const;
        void setTitle(const std::string& title);
        void setFullscreen(bool on);  // SDL3 borderless-desktop fullscreen
        void setVsync(bool on);       // swap interval 1/0 (context is current)

        // Read the BACK buffer (call after drawing, before swap) and write it
        // as an opaque PNG. False on failure; SDL_GetError() says why.
        bool saveScreenshot(const std::string& path) const;

        SDL_Window* handle() const { return m_window; }

    private:
        SDL_Window*   m_window = nullptr;
        SDL_GLContext m_glContext = nullptr;
    };

} // namespace engine
