#include "engine/Input.h"

namespace engine {

    void Input::newFrame() {
        for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
            m_keyPrev[i] = m_keyDown[i];
        }
        for (int i = 0; i < kMouseButtons; ++i) {
            m_mousePrev[i] = m_mouseDown[i];
        }
        m_relX = 0.0f;
        m_relY = 0.0f;
        m_wheelY = 0.0f;
    }

    void Input::handleEvent(const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_KEY_DOWN:
                if (e.key.scancode < SDL_SCANCODE_COUNT) {
                    m_keyDown[e.key.scancode] = true;
                }
                break;
            case SDL_EVENT_KEY_UP:
                if (e.key.scancode < SDL_SCANCODE_COUNT) {
                    m_keyDown[e.key.scancode] = false;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button < kMouseButtons) {
                    m_mouseDown[e.button.button] = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button < kMouseButtons) {
                    m_mouseDown[e.button.button] = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                m_relX += e.motion.xrel;
                m_relY += e.motion.yrel;
                m_mouseX = e.motion.x;
                m_mouseY = e.motion.y;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                m_wheelY += e.wheel.y;
                break;
            default:
                break;
        }
    }

    void Input::syncMousePosition() {
        // Motion events alone leave the position stale whenever the pointer
        // got where it is without one reaching us -- released from relative
        // mode, warped by the OS, or sitting still since before the window
        // opened -- and the game now DRAWS the pointer, so a stale position is
        // an arrow in the wrong place. Ask SDL where it actually is.
        float x = 0.0f, y = 0.0f;
        SDL_GetMouseState(&x, &y);
        m_mouseX = x;
        m_mouseY = y;
    }

    bool Input::isKeyDown(SDL_Scancode sc) const {
        return (sc >= 0 && sc < SDL_SCANCODE_COUNT) && m_keyDown[sc];
    }

    bool Input::wasKeyPressed(SDL_Scancode sc) const {
        return (sc >= 0 && sc < SDL_SCANCODE_COUNT) && m_keyDown[sc] && !m_keyPrev[sc];
    }

    bool Input::isMouseDown(int button) const {
        return button >= 0 && button < kMouseButtons && m_mouseDown[button];
    }

    bool Input::wasMousePressed(int button) const {
        return button >= 0 && button < kMouseButtons && m_mouseDown[button] && !m_mousePrev[button];
    }

    bool Input::wasMouseReleased(int button) const {
        return button >= 0 && button < kMouseButtons && !m_mouseDown[button] && m_mousePrev[button];
    }

} // namespace engine
