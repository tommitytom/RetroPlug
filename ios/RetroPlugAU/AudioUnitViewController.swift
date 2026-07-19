// The AUv3 extension's principal class. Hosts create the audio unit through
// AUAudioUnitFactory; the view is a minimal placeholder (the real RetroPlug
// UI is a later phase — see ios/README.md). The embedded mGB synth loads by
// default (silent until MIDI arrives); the buttons below let the user swap in
// a ROM from Files instead — whatever is loaded persists with the host
// project through fullState.
import CoreAudioKit
import RetroPlugKit
import UniformTypeIdentifiers

public class AudioUnitViewController: AUViewController, AUAudioUnitFactory, UIDocumentPickerDelegate {
    private var audioUnit: RetroPlugAudioUnit?
    private let statusLabel = UILabel()

    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let au = try RetroPlugAudioUnit(componentDescription: componentDescription, options: [])
        audioUnit = au
        return au
    }

    public override func viewDidLoad() {
        super.viewDidLoad()

        statusLabel.text = "RetroPlug mGB (spike)\nMIDI ch 1–4 → pu1 / pu2 / wav / noi"
        statusLabel.numberOfLines = 0
        statusLabel.textAlignment = .center

        let loadRomButton = UIButton(type: .system)
        loadRomButton.setTitle("Load ROM…", for: .normal)
        loadRomButton.addTarget(self, action: #selector(pickRom), for: .touchUpInside)

        let mgbButton = UIButton(type: .system)
        mgbButton.setTitle("Reset to mGB synth", for: .normal)
        mgbButton.addTarget(self, action: #selector(reloadMgb), for: .touchUpInside)

        let stack = UIStackView(arrangedSubviews: [statusLabel, loadRomButton, mgbButton])
        stack.axis = .vertical
        stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            stack.centerYAnchor.constraint(equalTo: view.centerYAnchor),
            stack.leadingAnchor.constraint(greaterThanOrEqualTo: view.leadingAnchor, constant: 16),
            view.trailingAnchor.constraint(greaterThanOrEqualTo: stack.trailingAnchor, constant: 16),
        ])
    }

    @objc private func pickRom() {
        let types = ["gb", "gbc"].compactMap { UTType(filenameExtension: $0) }
        // asCopy: the picked file is copied into the extension's sandbox, so
        // no security-scope bookkeeping. Sibling .sav pairing is app-only
        // (a picker scope never covers directory siblings); DAW sessions get
        // their SRAM back through fullState anyway.
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: types.isEmpty ? [.data] : types, asCopy: true)
        picker.delegate = self
        present(picker, animated: true)
    }

    @objc private func reloadMgb() {
        guard let au = audioUnit else { return }
        do {
            try au.loadEmbeddedMGB(withSram: nil)
            statusLabel.text = "mGB synth\nMIDI ch 1–4 → pu1 / pu2 / wav / noi"
        } catch {
            statusLabel.text = "mGB failed to load: \(error.localizedDescription)"
        }
    }

    public func documentPicker(_ controller: UIDocumentPickerViewController,
                               didPickDocumentsAt urls: [URL]) {
        guard let au = audioUnit, let url = urls.first else { return }
        do {
            let rom = try Data(contentsOf: url)
            try au.loadRomData(rom, sram: nil, state: nil)
            statusLabel.text = "\(url.lastPathComponent)\nSaves with the host project."
        } catch {
            statusLabel.text = "Load failed: \(error.localizedDescription)"
        }
    }
}
