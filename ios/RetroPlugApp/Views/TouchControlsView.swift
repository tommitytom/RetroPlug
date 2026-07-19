// Game Boy touch controls: a D-pad on one gesture surface (so diagonals and
// finger slides work) plus A/B and Select/Start press-and-hold buttons.
import RetroPlugKit
import SwiftUI

struct TouchControlsView: View {
    let press: (RPGameboyButton, Bool) -> Void

    var body: some View {
        HStack(alignment: .center, spacing: 24) {
            DPadView(press: press)
            Spacer(minLength: 0)
            VStack(spacing: 18) {
                HStack(spacing: 20) {
                    HoldButton(label: "B", press: { press(.b, $0) })
                    HoldButton(label: "A", press: { press(.a, $0) })
                }
                HStack(spacing: 12) {
                    PillHoldButton(label: "SELECT", press: { press(.select, $0) })
                    PillHoldButton(label: "START", press: { press(.start, $0) })
                }
            }
        }
    }
}

// One gesture surface for all four directions; the touched sector decides
// which buttons are down, and moving the finger re-resolves (diagonals are
// two sectors at once).
private struct DPadView: View {
    let press: (RPGameboyButton, Bool) -> Void
    @State private var active: Set<UInt8> = []

    private let size: CGFloat = 150

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(.systemGray4))
                .frame(width: size * 0.34, height: size)
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(.systemGray4))
                .frame(width: size, height: size * 0.34)
            Image(systemName: "dpad")
                .font(.title2)
                .foregroundStyle(.secondary)
        }
        .frame(width: size, height: size)
        .contentShape(Rectangle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { value in resolve(location: value.location) }
                .onEnded { _ in apply([]) }
        )
    }

    private func resolve(location: CGPoint) {
        let dx = location.x - size / 2
        let dy = location.y - size / 2
        let dead = size * 0.12
        var buttons: Set<UInt8> = []
        if dx > dead  { buttons.insert(RPGameboyButton.right.rawValue) }
        if dx < -dead { buttons.insert(RPGameboyButton.left.rawValue) }
        if dy < -dead { buttons.insert(RPGameboyButton.up.rawValue) }
        if dy > dead  { buttons.insert(RPGameboyButton.down.rawValue) }
        apply(buttons)
    }

    private func apply(_ buttons: Set<UInt8>) {
        for raw in buttons.subtracting(active) {
            if let button = RPGameboyButton(rawValue: raw) { press(button, true) }
        }
        for raw in active.subtracting(buttons) {
            if let button = RPGameboyButton(rawValue: raw) { press(button, false) }
        }
        active = buttons
    }
}

private struct HoldButton: View {
    let label: String
    let press: (Bool) -> Void
    @State private var down = false

    var body: some View {
        Text(label)
            .font(.headline)
            .frame(width: 58, height: 58)
            .background(Circle().fill(down ? Color.accentColor.opacity(0.5) : Color(.systemGray4)))
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if !down {
                            down = true
                            press(true)
                        }
                    }
                    .onEnded { _ in
                        down = false
                        press(false)
                    }
            )
    }
}

private struct PillHoldButton: View {
    let label: String
    let press: (Bool) -> Void
    @State private var down = false

    var body: some View {
        Text(label)
            .font(.caption2.weight(.semibold))
            .padding(.horizontal, 14)
            .padding(.vertical, 6)
            .background(Capsule().fill(down ? Color.accentColor.opacity(0.5) : Color(.systemGray4)))
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if !down {
                            down = true
                            press(true)
                        }
                    }
                    .onEnded { _ in
                        down = false
                        press(false)
                    }
            )
    }
}
