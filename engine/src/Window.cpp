// glad must come before any SDL header that could pull in GL.
#include "engine/GL.h"

#include "engine/Window.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {

    namespace {
        // Adapter so glad can load through SDL's proc-address lookup. Both sides
        // deal in generic function pointers (void(*)(void)).
        GLADapiproc glLoader(const char* name) {
            return reinterpret_cast<GLADapiproc>(SDL_GL_GetProcAddress(name));
        }
    } // namespace

    Window::Window(const std::string& title, int width, int height) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }

#ifdef __APPLE__
        // macOS ships only core profiles 3.2 and 4.1, and requires the
        // forward-compatible flag for any 3.2+ core context. GL 4.1 core is a
        // superset of 3.3, so the glad 3.3 loader and the #version 330 core
        // shaders run unchanged.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        // No SDL_WINDOW_HIGH_PIXEL_DENSITY: the UI draws and hit-tests in one
        // coordinate space, which is only safe while window points == pixels.
        // Retina needs a point->pixel pass over the UI first (ROADMAP gap).
        m_window = SDL_CreateWindow(title.c_str(), width, height,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!m_window) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

        m_glContext = SDL_GL_CreateContext(m_window);
        if (!m_glContext) {
            throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
        }

        SDL_GL_MakeCurrent(m_window, m_glContext);

        if (gladLoadGL(glLoader) == 0) {
            throw std::runtime_error("Failed to load OpenGL functions via glad");
        }

        SDL_GL_SetSwapInterval(1); // vsync
    }

    Window::~Window() {
        if (m_glContext) {
            SDL_GL_DestroyContext(m_glContext);
        }
        if (m_window) {
            SDL_DestroyWindow(m_window);
        }
        SDL_Quit();
    }

    void Window::swap() {
        SDL_GL_SwapWindow(m_window);
    }

    void Window::updateViewport() {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
        glViewport(0, 0, w, h);
    }

    int Window::width() const {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
        return w;
    }

    int Window::height() const {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
        return h;
    }

    float Window::aspect() const {
        const int h = height();
        return h > 0 ? static_cast<float>(width()) / static_cast<float>(h) : 1.0f;
    }

    void Window::setRelativeMouse(bool enabled) {
        SDL_SetWindowRelativeMouseMode(m_window, enabled);
    }

    void Window::setTitle(const std::string& title) {
        SDL_SetWindowTitle(m_window, title.c_str());
    }

    void Window::setFullscreen(bool on) {
        // SDL3 defaults to borderless desktop fullscreen (no exclusive mode
        // set). The per-frame aspect/viewport refresh in Application::run
        // absorbs the size change; nothing else needs to know.
        SDL_SetWindowFullscreen(m_window, on);
    }

    void Window::setVsync(bool on) {
        SDL_GL_SetSwapInterval(on ? 1 : 0);
    }

    bool Window::saveScreenshot(const std::string& path) const {
        const int w = width(), h = height();
        if (w <= 0 || h <= 0) {
            SDL_SetError("window has no drawable area");
            return false;
        }
        // RGB, not RGBA: UI blending leaves the back buffer's alpha below 1,
        // which would write a see-through PNG.
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) *
                                         static_cast<std::size_t>(h) * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

        SDL_Surface* surface =
            SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGB24, pixels.data(), w * 3);
        if (!surface) return false;
        // GL's origin is bottom-left; an image's is top-left.
        bool ok = SDL_FlipSurface(surface, SDL_FLIP_VERTICAL) &&
                  SDL_SavePNG(surface, path.c_str());
        SDL_DestroySurface(surface);
        return ok;
    }

} // namespace engine
