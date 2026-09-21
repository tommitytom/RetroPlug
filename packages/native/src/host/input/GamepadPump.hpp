#pragma once

#include "dpfjs/host/TjsHostRuntime.hpp" // brings quickjs
#include "host/input/GamepadManager.hpp"

/** Drain the gamepad manager onto the UI's event bus.
 *
 *  A ~35-line switch that the plugin editor and the SDL standalone carried identically - same four event
 *  kinds, same names, same argument counts, same null-name guards. The only differences were how each
 *  reached its LvglJsEngine and its GamepadManager, which are now parameters.
 *
 *  `Engine` is a template parameter rather than a concrete type because the two hosts hold their engine
 *  differently (a member vs a field on AppState) and it only has to answer `emit`. GamepadManager.cpp is
 *  already listed in both targets' sources, so this adds no link edge. */
template <class Engine>
inline void pumpGamepadInto(retroplug::GamepadManager& gamepad, Engine& engine, JSContext* ctx) {
    if (!ctx) return;
    gamepad.update([&engine, ctx](const retroplug::GamepadEvent& ev) {
        switch (ev.kind) {
            case retroplug::GamepadEvent::Kind::Connected: {
                JSValue args[2] = {JS_NewInt32(ctx, ev.pad), JS_NewString(ctx, ev.name ? ev.name : "")};
                engine.emit("gamepad-connected", 2, args);
                for (JSValue& v : args) JS_FreeValue(ctx, v);
                break;
            }
            case retroplug::GamepadEvent::Kind::Disconnected: {
                JSValue args[1] = {JS_NewInt32(ctx, ev.pad)};
                engine.emit("gamepad-disconnected", 1, args);
                JS_FreeValue(ctx, args[0]);
                break;
            }
            case retroplug::GamepadEvent::Kind::Button: {
                JSValue args[3] = {JS_NewInt32(ctx, ev.pad), JS_NewString(ctx, ev.button ? ev.button : ""),
                                   JS_NewBool(ctx, ev.pressed)};
                engine.emit("gamepad-button", 3, args);
                for (JSValue& v : args) JS_FreeValue(ctx, v);
                break;
            }
            case retroplug::GamepadEvent::Kind::Axis: {
                JSValue args[3] = {JS_NewInt32(ctx, ev.pad), JS_NewString(ctx, ev.axis ? ev.axis : ""),
                                   JS_NewFloat64(ctx, ev.value)};
                engine.emit("gamepad-axis", 3, args);
                for (JSValue& v : args) JS_FreeValue(ctx, v);
                break;
            }
        }
    });
}
