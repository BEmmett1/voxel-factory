#include "engine/Application.h"

namespace engine {

    namespace {
        constexpr double kTickSeconds = 1.0 / 20.0; // 20 Hz simulation
        constexpr int    kMaxTicksPerFrame = 8;     // avoid the spiral of death
        constexpr float  kMaxFrameDt = 0.1f;        // clamp long stalls
    }

    Application::Application(const std::string& title, int width, int height)
        : m_window(std::make_unique<Window>(title, width, height)) {}

    Application::~Application() = default;

    void Application::run() {
        m_running = true;
        onStart();

        while (m_running) {
            float dt = m_clock.tick();
            if (dt > kMaxFrameDt) dt = kMaxFrameDt;

            m_input.newFrame();
            processEvents();

            // Fixed-timestep simulation ticks, decoupled from render rate.
            // While paused, simulated time simply does not pass: nothing
            // accrues, so resuming never burst-runs a tick backlog.
            if (m_paused) {
                m_tickAccumulator = 0.0;
            } else {
                m_tickAccumulator += dt;
                int ticks = 0;
                while (m_tickAccumulator >= kTickSeconds && ticks < kMaxTicksPerFrame) {
                    onTick();
                    m_tickAccumulator -= kTickSeconds;
                    ++ticks;
                }
            }

            m_camera.aspect = m_window->aspect();
            m_window->updateViewport();

            onUpdate(dt);
            onRender();
            m_window->swap();
        }

        onExit(); // runs on every quit path (Esc, window close, ...)
    }

    void Application::processEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                m_running = false;
            } else if (e.type == SDL_EVENT_KEY_DOWN &&
                       e.key.scancode == SDL_SCANCODE_ESCAPE && !e.key.repeat) {
                onEscape();
            }
            m_input.handleEvent(e);
        }
    }

} // namespace engine
