// Minimal LSDj .sav reader — the Swift twin of the desktop codec
// (packages/retroplug/src/lsdj/codec/sav.ts + rle.ts), covering just what the
// song manager needs: list the stored songs, and rebuild an SRAM image with a
// chosen song decompressed into working memory. Songs stay raw 0x8000-byte
// blobs (no per-field decode) — the emulator is the editor here. Layout:
// working song (raw, offset 0) + 512-byte header at 0x8000 (names, versions,
// 'jk' magic, active-project index, 191-entry block allocation table) + the
// RLE-compressed stored-project block area.
import Foundation

enum LsdjSavError: LocalizedError {
    case tooSmall
    case badMagic
    case slotEmpty
    case malformed(String)

    var errorDescription: String? {
        switch self {
        case .tooSmall:           return "Save file is smaller than an LSDj .sav."
        case .badMagic:           return "Not an LSDj save (missing 'jk' marker)."
        case .slotEmpty:          return "That song slot is empty."
        case .malformed(let why): return "Corrupt LSDj save: \(why)."
        }
    }
}

enum LsdjSav {
    static let savSize = 0x20000 // 128 KiB
    private static let songBytes = 0x8000
    private static let projectNames = 0x8000 // [32][8]
    private static let projectVers = 0x8100 // [32]
    private static let initMagic = 0x813e // 'j','k'
    private static let activeProj = 0x8140
    private static let allocTable = 0x8141 // [191]
    private static let blockArea = 0x8200
    private static let blockSize = 0x200
    private static let blockCount = 191
    private static let emptyBlock: UInt8 = 0xff
    private static let nameLen = 8
    private static let projectCount = 32

    struct Song: Identifiable, Equatable {
        let slot: Int      // 0-31 project index
        let name: String   // up to 8 chars
        let version: UInt8 // LSDj's per-save edit counter
        var id: Int { slot }
    }

    // Data slices index from their parent's offsets; rebase so the byte-offset
    // constants above are valid subscripts.
    private static func rebased(_ data: Data) -> Data {
        data.startIndex == 0 ? data : Data(data)
    }

    /// Cheap sniff for pickers: full-size image carrying the 'jk' SRAM marker.
    static func isLikelySav(_ data: Data) -> Bool {
        let data = rebased(data)
        return data.count >= savSize && data[initMagic] == 0x6a && data[initMagic + 1] == 0x6b
    }

    /// Slot LSDj considers loaded into working memory (nil when none / 0xFF).
    static func activeSlot(in sav: Data) -> Int? {
        let sav = rebased(sav)
        guard sav.count >= savSize else { return nil }
        let v = Int(sav[activeProj])
        return v < projectCount ? v : nil
    }

    /// The stored songs, in slot order. Empty for a non-LSDj image.
    static func songs(in sav: Data) -> [Song] {
        let sav = rebased(sav)
        guard isLikelySav(sav) else { return [] }
        // A slot exists iff at least one block in the allocation table carries
        // its index (matching the desktop decoder's walk).
        var present = Set<Int>()
        for i in 0..<blockCount {
            let p = Int(sav[allocTable + i])
            if p < projectCount { present.insert(p) }
        }
        return present.sorted().map { slot in
            var name = ""
            for i in 0..<nameLen {
                let c = sav[projectNames + slot * nameLen + i]
                if c == 0 { break }
                name.append(Character(UnicodeScalar(c)))
            }
            return Song(slot: slot, name: name, version: sav[projectVers + slot])
        }
    }

    /// True when working memory is byte-identical to the active slot's stored
    /// song — i.e. nothing to lose by overwriting it. nil when that can't be
    /// determined (no active slot, or a malformed archive entry) — callers
    /// should treat nil as "might be dirty".
    static func workingSongIsClean(in sav: Data) -> Bool? {
        let sav = rebased(sav)
        guard isLikelySav(sav), let slot = activeSlot(in: sav) else { return nil }
        guard let entry = (0..<blockCount).first(where: { Int(sav[allocTable + $0]) == slot })
        else { return nil }
        guard let stored = try? decompress(
            blockArea: sav.subdata(in: blockArea..<(blockArea + blockCount * blockSize)),
            startBlock: entry)
        else { return nil }
        return sav.subdata(in: 0..<songBytes) == stored
    }

    /// A new SRAM image with `slot`'s stored song decompressed into working
    /// memory and marked active. The archive itself is untouched, so LSDj's
    /// own LOAD/SAVE screen still sees every project.
    static func loadingSong(slot: Int, into sav: Data) throws -> Data {
        let sav = rebased(sav)
        guard sav.count >= savSize else { throw LsdjSavError.tooSmall }
        guard isLikelySav(sav) else { throw LsdjSavError.badMagic }
        guard let entry = (0..<blockCount).first(where: { Int(sav[allocTable + $0]) == slot })
        else { throw LsdjSavError.slotEmpty }

        let song = try decompress(blockArea: sav.subdata(in: blockArea..<(blockArea + blockCount * blockSize)),
                                  startBlock: entry)
        var out = sav
        out.replaceSubrange(0..<songBytes, with: song)
        out[activeProj] = UInt8(slot)
        return out
    }

    // LSDj's RLE block stream (port of the desktop rle.ts decompressProject):
    // 0xC0 = run, 0xE0 = special (default wave/instrument stamps, block jump,
    // 0xFF EOF). Returns the raw 0x8000-byte song.
    private static func decompress(blockArea: Data, startBlock: Int) throws -> Data {
        let rle: UInt8 = 0xc0, sa: UInt8 = 0xe0
        let defaultWaveByte: UInt8 = 0xf0, defaultInstrByte: UInt8 = 0xf1
        let defaultWave: [UInt8] = [0x8e, 0xcd, 0xcc, 0xbb, 0xaa, 0xa9, 0x99, 0x88,
                                    0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31]
        let defaultInstrument: [UInt8] = [0xa8, 0x00, 0x00, 0xff, 0x00, 0x00, 0x03, 0x00,
                                          0x00, 0xd0, 0x00, 0x00, 0x00, 0xf3, 0x00, 0x00]

        var out = Data(capacity: songBytes)
        func push(_ v: UInt8) throws {
            guard out.count < songBytes else { throw LsdjSavError.malformed("song overflows 0x8000 bytes") }
            out.append(v)
        }

        var pos = 0
        func rd() throws -> UInt8 {
            guard pos < blockArea.count else { throw LsdjSavError.malformed("read past end of block area") }
            defer { pos += 1 }
            return blockArea[blockArea.startIndex + pos]
        }

        var curBlock = startBlock
        for _ in 0...blockCount {
            pos = curBlock * blockSize
            var nextJump = -1
            while nextJump < 0 {
                let byte = try rd()
                if byte == rle {
                    let b = try rd()
                    if b == rle {
                        try push(rle)
                    } else {
                        let c = try rd()
                        for _ in 0..<Int(c) { try push(b) }
                    }
                } else if byte == sa {
                    let a = try rd()
                    if a == sa {
                        try push(sa)
                    } else if a == defaultWaveByte {
                        let c = try rd()
                        for _ in 0..<Int(c) { for v in defaultWave { try push(v) } }
                    } else if a == defaultInstrByte {
                        let c = try rd()
                        for _ in 0..<Int(c) { for v in defaultInstrument { try push(v) } }
                    } else {
                        nextJump = Int(a) // block jump or EOF
                    }
                } else {
                    try push(byte)
                }
            }
            if nextJump == Int(emptyBlock) { break } // 0xFF = EOF
            let target = nextJump - 1 // 1-based jump -> 0-based block
            guard target >= 0, target < blockCount else {
                throw LsdjSavError.malformed("block jump out of range")
            }
            curBlock = target
        }

        guard out.count == songBytes else { throw LsdjSavError.malformed("song is not 0x8000 bytes") }
        return out
    }
}
