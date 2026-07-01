#pragma once

#include <SDL3/SDL.h>

namespace engine {

    // Per-frame keyboard/mouse state with edge detection. The Application feeds
    // it SDL events each frame; game code queries it in onUpdate.
    class Input {
    public:
        // Call once at the start of each frame, before polling events.
        void newFrame();
        void handleEvent(const SDL_Event& e);

        bool isKeyDown(SDL_Scancode sc) const;
        bool wasKeyPressed(SDL_Scancode sc) const;   // true only on the frame it went down

        bool isMouseDown(int button) const;
        bool wasMousePressed(int button) const;

        // Relative mouse motion accumulated this frame (for FPS look).
        float mouseRelX() const { return m_relX; }
        float mouseRelY() const { return m_relY; }

        // Whole wheel steps this frame (positive = scrolled up/away).
        int wheelSteps() const { return static_cast<int>(m_wheelY); }

        // Absolute cursor position in window coordinates (meaningful while
        // relative mouse mode is off, e.g. inside menus).
        float mouseX() const { return m_mouseX; }
        float mouseY() const { return m_mouseY; }

    private:
        static constexpr int kMouseButtons = 8;

        bool  m_keyDown[SDL_SCANCODE_COUNT] = {};
        bool  m_keyPrev[SDL_SCANCODE_COUNT] = {};
        bool  m_mouseDown[kMouseButtons] = {};
        bool  m_mousePrev[kMouseButtons] = {};
        float m_relX = 0.0f;
        float m_relY = 0.0f;
        float m_wheelY = 0.0f;
        float m_mouseX = 0.0f;
        float m_mouseY = 0.0f;
    };

} // namespace engine
