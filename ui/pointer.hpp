#pragma once

#include <SDL.h>

namespace ui {

// Mouse and touch become the same pointer events, so everything built on the
// PC with a mouse works with fingers on the Pi. Pinch arrives as two pointers.
struct Pointer {
    enum Type { none, down, move, up, wheel } type = none;
    SDL_FingerID id = 0; // -1 for the mouse
    float x = 0, y = 0;  // logical screen pixels
    float scroll = 0;
};

inline Pointer pointer_from(const SDL_Event& event, int width, int height) {
    Pointer p;
    switch (event.type) {
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (event.button.button != SDL_BUTTON_LEFT || event.button.which == SDL_TOUCH_MOUSEID) {
            break;
        }
        p.type = event.type == SDL_MOUSEBUTTONDOWN ? Pointer::down : Pointer::up;
        p.id = -1;
        p.x = float(event.button.x);
        p.y = float(event.button.y);
        break;
    case SDL_MOUSEMOTION:
        if (event.motion.which == SDL_TOUCH_MOUSEID) {
            break;
        }
        p.type = Pointer::move;
        p.id = -1;
        p.x = float(event.motion.x);
        p.y = float(event.motion.y);
        break;
    case SDL_MOUSEWHEEL: {
        int x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        p.type = Pointer::wheel;
        p.id = -1;
        p.x = float(x);
        p.y = float(y);
        p.scroll = event.wheel.preciseY;
        break;
    }
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION:
        p.type = event.type == SDL_FINGERDOWN ? Pointer::down : event.type == SDL_FINGERUP ? Pointer::up : Pointer::move;
        p.id = event.tfinger.fingerId;
        p.x = event.tfinger.x * float(width);
        p.y = event.tfinger.y * float(height);
        break;
    default:
        break;
    }
    return p;
}

} // namespace ui
