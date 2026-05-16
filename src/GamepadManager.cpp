#include "GamepadManager.hpp"

#include <SDL.h>
#include "DistrhoUtils.hpp"

#include <algorithm>
#include <array>

namespace retroplug {

namespace {

// Axis dead-zone in raw SDL units (-32768..32767). Below this the axis
// reports 0.0 — keeps idle stick drift from spamming the JS bridge. Above
// it, the value is rescaled to the dead-zone..32767 range so a fresh push
// off centre still starts near zero rather than jumping.
constexpr int kAxisDeadzone = 8000;

// Axis-change threshold (in normalised -1..1 units). The previous value
// must move at least this much before we emit a new axis event. Stops
// every wobble byte from crossing the C++/JS boundary.
constexpr float kAxisEmitThreshold = 0.05f;

float normalizeAxis(int16_t raw)
{
    const int v = static_cast<int>(raw);
    if (v > -kAxisDeadzone && v < kAxisDeadzone) return 0.0f;
    const float scale = static_cast<float>(32767 - kAxisDeadzone);
    if (v > 0) return std::min(1.0f, static_cast<float>(v - kAxisDeadzone) / scale);
    return std::max(-1.0f, static_cast<float>(v + kAxisDeadzone) / scale);
}

} // namespace

struct GamepadManager::PadState {
    SDL_GameController* controller = nullptr;
    SDL_JoystickID id = -1;
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> buttons{};
    std::array<float, SDL_CONTROLLER_AXIS_MAX>  axes{};
};

GamepadManager::GamepadManager()
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        d_stderr("[Gamepad] SDL_InitSubSystem(GAMECONTROLLER) failed: %s",
                 SDL_GetError());
        return;
    }
    // We poll state directly via SDL_GameController* getters; we never
    // SDL_PollEvent. Tell SDL not to queue controller events so they don't
    // sit in the process-global queue alongside whatever the host might
    // also be doing with SDL.
    SDL_GameControllerEventState(SDL_IGNORE);
    SDL_JoystickEventState(SDL_IGNORE);
    ok_ = true;
}

GamepadManager::~GamepadManager()
{
    for (auto& p : pads_) {
        if (p && p->controller) SDL_GameControllerClose(p->controller);
    }
    pads_.clear();
    if (ok_) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void GamepadManager::update(const std::function<void(const GamepadEvent&)>& sink)
{
    if (!ok_) return;

    // Refresh device state. Drives hot-plug bookkeeping internally and
    // updates per-axis / per-button values that SDL_GameControllerGet*
    // returns below.
    SDL_GameControllerUpdate();

    // --- Detect new devices --------------------------------------------------
    const int numJoy = SDL_NumJoysticks();
    for (int i = 0; i < numJoy; ++i) {
        if (!SDL_IsGameController(i)) continue;
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
        if (id < 0) continue;
        const bool known = std::any_of(pads_.begin(), pads_.end(),
            [id](const std::unique_ptr<PadState>& p) { return p && p->id == id; });
        if (known) continue;

        SDL_GameController* gc = SDL_GameControllerOpen(i);
        if (!gc) {
            d_stderr("[Gamepad] SDL_GameControllerOpen(%d) failed: %s",
                     i, SDL_GetError());
            continue;
        }
        auto pad = std::make_unique<PadState>();
        pad->controller = gc;
        pad->id = id;
        const char* name = SDL_GameControllerName(gc);
        d_stdout("[Gamepad] connected pad=%d name=\"%s\"", id, name ? name : "?");

        GamepadEvent ev{};
        ev.kind = GamepadEvent::Kind::Connected;
        ev.pad = id;
        ev.name = name ? name : "";
        sink(ev);

        pads_.push_back(std::move(pad));
    }

    // --- Detect removed devices ---------------------------------------------
    for (auto it = pads_.begin(); it != pads_.end(); ) {
        PadState* p = it->get();
        if (!p || !p->controller || !SDL_GameControllerGetAttached(p->controller)) {
            const SDL_JoystickID id = p ? p->id : -1;
            d_stdout("[Gamepad] disconnected pad=%d", id);
            GamepadEvent ev{};
            ev.kind = GamepadEvent::Kind::Disconnected;
            ev.pad = id;
            sink(ev);
            if (p && p->controller) SDL_GameControllerClose(p->controller);
            it = pads_.erase(it);
        } else {
            ++it;
        }
    }

    // --- Diff current state vs last frame -----------------------------------
    for (auto& padPtr : pads_) {
        PadState& p = *padPtr;
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
            const auto btn = static_cast<SDL_GameControllerButton>(b);
            const bool pressed =
                SDL_GameControllerGetButton(p.controller, btn) != 0;
            if (pressed == p.buttons[b]) continue;
            p.buttons[b] = pressed;
            GamepadEvent ev{};
            ev.kind = GamepadEvent::Kind::Button;
            ev.pad = p.id;
            ev.button = SDL_GameControllerGetStringForButton(btn);
            ev.pressed = pressed;
            sink(ev);
        }
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
            const auto axis = static_cast<SDL_GameControllerAxis>(a);
            const float value = normalizeAxis(
                SDL_GameControllerGetAxis(p.controller, axis));
            // Always emit when crossing the zero boundary so JS sees the
            // "stick returned to centre" frame even if the previous value
            // hadn't hit kAxisEmitThreshold.
            const float prev = p.axes[a];
            const bool crossedZero = (prev == 0.0f) != (value == 0.0f);
            if (!crossedZero &&
                std::abs(value - prev) < kAxisEmitThreshold) continue;
            p.axes[a] = value;
            GamepadEvent ev{};
            ev.kind = GamepadEvent::Kind::Axis;
            ev.pad = p.id;
            ev.axis = SDL_GameControllerGetStringForAxis(axis);
            ev.value = value;
            sink(ev);
        }
    }
}

} // namespace retroplug
