// The Game Boy LCD, shared by the app and the AUv3 extension. A CADisplayLink
// pulls the latest emulator frame through an injected closure (so this file
// needs no RetroPlugKit import) and blits it to a CALayer as a CGImage —
// 160×144×4 bytes at 60 Hz is trivial, and keeping SwiftUI's view graph out
// of the hot loop avoids per-frame invalidation entirely.
import SwiftUI
import UIKit

/// Copies the latest frame into the buffer (capacity in pixels). Returns
/// false when no frame is available yet — the previous image stays up.
typealias GameBoyFrameSource = (UnsafeMutablePointer<UInt32>, Int) -> Bool

struct GameBoyScreenView: UIViewRepresentable {
    // LCD geometry; matches RPScreenWidth/RPScreenHeight in RetroPlugKit.
    static let pixelWidth = 160
    static let pixelHeight = 144

    let frameSource: GameBoyFrameSource

    func makeUIView(context: Context) -> GameBoyScreenUIView {
        let view = GameBoyScreenUIView()
        view.frameSource = frameSource
        return view
    }

    func updateUIView(_ uiView: GameBoyScreenUIView, context: Context) {
        uiView.frameSource = frameSource
    }
}

final class GameBoyScreenUIView: UIView {
    var frameSource: GameBoyFrameSource?

    private var displayLink: CADisplayLink?
    private var pixels = [UInt32](repeating: 0,
                                  count: GameBoyScreenView.pixelWidth * GameBoyScreenView.pixelHeight)

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .black
        isUserInteractionEnabled = false
        // Crisp integer-pixel look at any scale.
        layer.magnificationFilter = .nearest
        layer.minificationFilter = .nearest
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    // The display link retains its target; starting/stopping on window
    // attach/detach both breaks that cycle and pauses rendering offscreen.
    override func didMoveToWindow() {
        super.didMoveToWindow()
        window == nil ? stopDisplayLink() : startDisplayLink()
    }

    private func startDisplayLink() {
        guard displayLink == nil else { return }
        let link = CADisplayLink(target: self, selector: #selector(tick))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
        link.add(to: .main, forMode: .common)
        displayLink = link
    }

    private func stopDisplayLink() {
        displayLink?.invalidate()
        displayLink = nil
    }

    @objc private func tick() {
        guard let frameSource else { return }
        let width = GameBoyScreenView.pixelWidth
        let height = GameBoyScreenView.pixelHeight

        let hasFrame = pixels.withUnsafeMutableBufferPointer { buffer in
            frameSource(buffer.baseAddress!, buffer.count)
        }
        guard hasFrame else { return }

        // Frames are XRGB8888 — little-endian B,G,R,X in memory. This exact
        // bitmapInfo is load-bearing: anything else swaps red and blue.
        let bitmapInfo = CGBitmapInfo(rawValue: CGBitmapInfo.byteOrder32Little.rawValue |
                                                CGImageAlphaInfo.noneSkipFirst.rawValue)
        let image: CGImage? = pixels.withUnsafeBytes { raw in
            guard let base = raw.baseAddress,
                  let data = CFDataCreate(nil, base.assumingMemoryBound(to: UInt8.self), raw.count),
                  let provider = CGDataProvider(data: data) else { return nil }
            return CGImage(width: width,
                           height: height,
                           bitsPerComponent: 8,
                           bitsPerPixel: 32,
                           bytesPerRow: width * 4,
                           space: CGColorSpaceCreateDeviceRGB(),
                           bitmapInfo: bitmapInfo,
                           provider: provider,
                           decode: nil,
                           shouldInterpolate: false,
                           intent: .defaultIntent)
        }
        if let image {
            layer.contents = image
        }
    }
}
