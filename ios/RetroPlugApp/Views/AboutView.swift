// Acknowledgements & licenses — the attribution the bundled open-source
// components require (SameBoy and reflect-cpp are MIT/Expat, which mandate
// reproducing the copyright notice; mGB is GPL-2.0, attributed with a source
// pointer). Reached from Settings.
import SwiftUI

struct AboutView: View {
    private var version: String {
        let short = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.0"
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "1"
        return "\(short) (\(build))"
    }

    var body: some View {
        Form {
            Section {
                LabeledContent("Version", value: version)
            } footer: {
                Text("A retro handheld emulator and chiptune instrument. Load your own program files, or play the built-in mGB synthesizer over MIDI.")
            }

            Section("Open-source components") {
                NavigationLink("SameBoy — emulation core") {
                    LicenseTextView(title: "SameBoy",
                                    subtitle: "Copyright © 2015–2026 Lior Halphon",
                                    text: Licenses.expat)
                }
                NavigationLink("mGB — built-in synthesizer") {
                    LicenseTextView(title: "mGB",
                                    subtitle: "Copyright © trash80 (Timothy Lamb)",
                                    text: Licenses.mgb)
                }
                NavigationLink("reflect-cpp") {
                    LicenseTextView(title: "reflect-cpp",
                                    subtitle: "Copyright © 2023–2025 Code17 GmbH",
                                    text: Licenses.expat)
                }
            }
        }
        .navigationTitle("Acknowledgements")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct LicenseTextView: View {
    let title: String
    let subtitle: String
    let text: String

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text(subtitle).font(.subheadline).foregroundStyle(.secondary)
                Text(text).font(.caption.monospaced())
            }
            .padding()
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .navigationTitle(title)
        .navigationBarTitleDisplayMode(.inline)
    }
}

private enum Licenses {
    // The MIT / Expat license body — SameBoy and reflect-cpp both use it; the
    // per-component copyright line is shown above the text.
    static let expat = """
    Permission is hereby granted, free of charge, to any person obtaining a copy \
    of this software and associated documentation files (the "Software"), to deal \
    in the Software without restriction, including without limitation the rights \
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell \
    copies of the Software, and to permit persons to whom the Software is \
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all \
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR \
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, \
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE \
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER \
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, \
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE \
    SOFTWARE.
    """

    static let mgb = """
    mGB is the Game Boy MIDI synthesizer program by trash80 (Timothy Lamb), \
    bundled unmodified as the built-in cartridge.

    mGB is free software, released under the GNU General Public License \
    version 2. Its complete corresponding source code is available at:

    https://github.com/trash80/mGB

    This program is distributed in the hope that it will be useful, but WITHOUT \
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or \
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for \
    more details: https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
    """
}
