// glad must come before any SDL header that could pull in GL.
#include "engine/GL.h"

#include "engine/Window.h"

#include <stdexcept>
#include <string>

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

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

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

} // namespace engine
