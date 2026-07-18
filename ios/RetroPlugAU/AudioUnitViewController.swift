// The AUv3 extension's principal class. Hosts create the audio unit through
// AUAudioUnitFactory; the view is a minimal placeholder (the real RetroPlug
// UI is a later phase — see ios/README.md).
import CoreAudioKit
import RetroPlugKit

public class AudioUnitViewController: AUViewController, AUAudioUnitFactory {
    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        try RetroPlugAudioUnit(componentDescription: componentDescription, options: [])
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        let label = UILabel()
        label.text = "RetroPlug mGB (spike)\nMIDI ch 1–4 → pu1 / pu2 / wav / noi"
        label.numberOfLines = 0
        label.textAlignment = .center
        label.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(label)
        NSLayoutConstraint.activate([
            label.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            label.centerYAnchor.constraint(equalTo: view.centerYAnchor),
        ])
    }
}
