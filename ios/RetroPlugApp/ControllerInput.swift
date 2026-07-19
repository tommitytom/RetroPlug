// Physical game controller support (MFi / PlayStation / Xbox): maps the
// extended-gamepad profile onto Game Boy buttons. Every callback arrives on
// the main queue (GCDevice.handlerQueue's default), so this feeds the same
// main-thread-only CoreBridge path the touch controls use.
import Foundation
import GameController
import RetroPlugKit

final class ControllerInput {
    private let press: (RPGameboyButton, Bool) -> Void
    private let connectionChanged: (Bool) -> Void
    private var observers: [NSObjectProtocol] = []

    init(press: @escaping (RPGameboyButton, Bool) -> Void,
         connectionChanged: @escaping (Bool) -> Void) {
        self.press = press
        self.connectionChanged = connectionChanged
        let center = NotificationCenter.default
        for name in [Notification.Name.GCControllerDidConnect, .GCControllerDidDisconnect] {
            observers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                self?.refresh()
            })
        }
        refresh()
    }

    deinit {
        observers.forEach(NotificationCenter.default.removeObserver)
    }

    private func refresh() {
        let pads = GCController.controllers().compactMap(\.extendedGamepad)
        pads.forEach(attach)
        connectionChanged(!pads.isEmpty)
    }

    private func attach(_ pad: GCExtendedGamepad) {
        // Positional (Nintendo) mapping: the east button is Game Boy A and the
        // south button Game Boy B, matching where they sit on a real DMG.
        bind(pad.buttonB, to: .a)
        bind(pad.buttonA, to: .b)
        bind(pad.dpad.up, to: .up)
        bind(pad.dpad.down, to: .down)
        bind(pad.dpad.left, to: .left)
        bind(pad.dpad.right, to: .right)
        // Menu/Options double as Start/Select. Claim them from the system
        // gesture recognizer, or Start presses arrive delayed or swallowed.
        pad.buttonMenu.preferredSystemGestureState = .alwaysReceive
        bind(pad.buttonMenu, to: .start)
        if let options = pad.buttonOptions {
            options.preferredSystemGestureState = .alwaysReceive
            bind(options, to: .select)
        }
    }

    private func bind(_ input: GCControllerButtonInput, to button: RPGameboyButton) {
        input.pressedChangedHandler = { [weak self] _, _, pressed in
            self?.press(button, pressed)
        }
    }
}
