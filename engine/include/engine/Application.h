#pragma once

#include "engine/Window.h"
#include "engine/Audio.h"
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
        Audio&  audio()  { return *m_audio; }
        Input&  input()  { return m_input; }
        Camera& camera() { return m_camera; }

        void quit() { m_running = false; }

        // While paused, simulation time does not accrue: onTick() stops and no
        // backlog builds up for resume. onUpdate/onRender keep running so a
        // pause menu can draw and take input.
        void setPaused(bool p) { m_paused = p; }
        bool paused() const { return m_paused; }

    private:
        void processEvents();

        std::unique_ptr<Window> m_window;
        std::unique_ptr<Audio>  m_audio; // after Window: dies before SDL_Quit()
        Camera m_camera;
        Input  m_input;
        Clock  m_clock;
        bool   m_running = false;
        bool   m_paused = false;
        double m_tickAccumulator = 0.0;
    };

} // namespace engine
