#pragma once

#include "engine/Window.h"
#include "engine/Input.h"
#include "engine/Camera.h"
#include "engine/Clock.h"

#include <memory>
#include <string>

namespace engine {

    // Base class for a game. The engine owns the main loop, window, camera, and
    // input; games subclass this and override the hooks. Mirrors the structure
    // of the potion-game engine so the two projects feel the same.
    class Application {
    public:
        Application(const std::string& title, int width, int height);
        virtual ~Application();

        void run();

    protected:
        // Hooks — games override these.
        virtual void onStart() {}
        virtual void onUpdate(float /*dt*/) {}
        virtual void onRender() {}
        virtual void onTick() {}            // fixed 20 Hz simulation step
        virtual void onEscape() { quit(); }
        virtual void onExit() {}            // after the loop ends (any quit path)

        Window& window() { return *m_window; }
        Input&  input()  { return m_input; }
        Camera& camera() { return m_camera; }

        void quit() { m_running = false; }

    private:
        void processEvents();

        std::unique_ptr<Window> m_window;
        Camera m_camera;
        Input  m_input;
        Clock  m_clock;
        bool   m_running = false;
        double m_tickAccumulator = 0.0;
    };

} // namespace engine
