// SDL2-backed game controller polling. Owned by LVGLPluginUI; polled from
// uiIdle() on the UI thread. Avoids SDL's event queue (which is process-
// global and would conflict with hosts that also use SDL) — uses
// SDL_GameControllerUpdate + direct state queries with frame-to-frame
// diffing to synthesise press / release / axis-cross events.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct _SDL_GameController;
typedef struct _SDL_GameController SDL_GameController;

namespace retroplug {

struct GamepadEvent {
    enum class Kind { Connected, Disconnected, Button, Axis };
    Kind kind;
    int32_t pad;               // SDL_JoystickID — stable across hot-plug
    const char* name = nullptr;    // Connected only (SDL-owned string)
    const char* button = nullptr;  // Button only  (SDL-owned string)
    bool pressed = false;          // Button only
    const char* axis = nullptr;    // Axis only   (SDL-owned string)
    float value = 0.0f;            // Axis only, -1.0..1.0
};

class GamepadManager {
public:
    GamepadManager();
    ~GamepadManager();

    GamepadManager(const GamepadManager&) = delete;
    GamepadManager& operator=(const GamepadManager&) = delete;

    // Returns false if SDL_INIT_GAMECONTROLLER failed (no gamepad work will
    // happen; update() becomes a no-op). The error is logged once via
    // d_stderr; callers don't need to do anything else.
    bool ok() const { return ok_; }

    // Poll connected controllers, emit transitions through `sink`. Call once
    // per UI frame from uiIdle(). `sink` runs synchronously on the UI thread.
    void update(const std::function<void(const GamepadEvent&)>& sink);

private:
    struct PadState;

    bool ok_ = false;
    std::vector<std::unique_ptr<PadState>> pads_;
};

} // namespace retroplug
