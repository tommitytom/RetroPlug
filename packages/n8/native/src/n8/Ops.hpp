#pragma once

// The operations of several Edio commands (docs/modular-graph-phase-e2-design.md section 4): a file read or written
// whole and a ROM booted through the menu, shared by the CLI's sessions (through the pack's facet) and the Pico box.
// Each is a fixed sequence of Edio and Menu calls, so its wire is theirs. Each answers a Status whose `step` names the
// step that failed, so a caller keeps its own policy around it: the box skips the upload when its saved CRC matches,
// and re-uploads and retries once when an install fails. One Progress reports the bytes and cancels: its answer is
// taken at the stop points each operation lists, which are between whole commands (Edio.hpp's Progress).
//
// What they leave on a failure is what the failure found: a file a failed call left open stays open (the reply's tail
// may still be on its way, so the caller flushes input and closes it if it goes on), and only a cancel, which comes
// between commands, closes what it opened. Portable, as Edio is.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "n8/Edio.hpp"

namespace rp::n8 {

// A run of bytes a caller owns, for the length of a call.
struct ByteView {
    const std::uint8_t* data = nullptr;
    std::size_t         size = 0;
};

// Where readFile puts a file once its listing has sized it: `buffer(ctx, size)` answers a buffer of at least `size`
// bytes, or null for none (TooLarge, Where::Buffer, nothing opened). How a caller that can allocate (the facet) reads a
// file it does not know the size of with one listing, as a fixed buffer would need two.
struct FileSink {
    std::uint8_t* (*buffer)(void* ctx, std::size_t size) = nullptr;
    void* ctx = nullptr;
};

// A whole SD file read: its size found by listing its directory (no separate file-info command), the sink asked for a
// buffer that size, then fileOpen(FA_READ), fileRead and fileClose. `size` gets the file's size once the listing has
// found it, so a TooLarge (step List: the listing sized it, nothing opened) says how large a buffer it needs. NotFound
// when the directory holds no file of that name. Stop points: before the open, and before each 512-byte block after
// the first (a cancel there closes the file), each asked once. Steps: List, Open, Data, Close. A path past the 16-bit
// length that prefixes it is TooLarge at Open, refused before any byte.
Status readFile(Edio& edio, std::string_view path, FileSink sink, std::size_t* size, Progress progress = {});
// The same into `buffer`, `capacity` bytes, as a caller with no heap (the box) reads one.
Status readFile(Edio& edio, std::string_view path, std::uint8_t* buffer, std::size_t capacity, std::size_t* size,
                Progress progress = {});

// A whole file written to an SD path, made with any missing parent directories (FA_WRITE | FA_CREATE_ALWAYS |
// FS_MAKEPATH, which empties a file already there), as one fileWrite. The inverse of readFile. Stop point: before the
// open; each acknowledged block reports. Steps: Open, Data, Close. A path past the 16-bit length (Where::Length) or
// data past a 32-bit count (Where::Size) is TooLarge at Open, refused before any byte.
Status writeFile(Edio& edio, std::string_view path, ByteView data, Progress progress = {});

// Whether bootRom writes the ROM to the card first, or boots one already there.
enum class Upload : std::uint8_t { Always, Never };

// The SD path the menu loads a game's battery from at its boot (and writes it back to at the next return to the menu):
// EDN8/gamedata/<the ROM's file name>/bram.srm, into `out`. False when it does not fit `capacity` (NUL included).
// bootRom's buffer fits any name FatFs can hold: 255 UTF-16 units, so at most 765 bytes of UTF-8 (a unit is at most
// three bytes; a four-byte sequence is two units). A name it does not fit is longer than any FatFs name, and bootRom
// answers it TooLarge at Where::BatteryPath (Step::Save) before anything is sent.
bool batteryPath(std::string_view romPath, char* out, std::size_t capacity);

// What a caller with a boot policy of its own runs around bootRom's steps (the Pico box: its handshake retries, its
// saved choice); the defaults are the plain boot. `handshake` false leaves out bootRom's '*t', for a caller that has
// just had the menu's 'k' itself (the test's stop point goes with it). `before(ctx, step)` is called before each step
// bootRom runs (Test, Upload, Save, Install, Start), between whole commands with the Edio idle, so unlike a Progress it
// may use the Edio (the box writes its box.cfg before the install); false stops the boot there, Cancelled at that step,
// with nothing of it sent.
struct BootOptions {
    bool handshake = true;
    bool (*before)(void* ctx, Step step) = nullptr;
    void* ctx = nullptr;
};

// Boot a ROM by driving the menu: test ('*t'), the ROM written to `path` when `upload` is Always, the battery (`srm`,
// when not empty) written to batteryPath(path) so the MENU copies it into cart SRAM at the hand-off (writing the
// cart's SRAM directly over USB corrupts the running menu, which uses that region), install ('*n' and `path`), start
// ('*s'). `mapIndex` gets what the install answered. Progress counts the bytes written, of the ROM and the battery
// together. Stop points: before the test (with the handshake), before each file's open, and before the install, each
// after `options`' before() for that step. A stop point's values may repeat the last report's, since one file's end and
// the next stop point's start are the same count of bytes, so a Progress that counts asks sees each stop point once.
// Steps: Test, Upload, Save, Install, Start. What cannot finish is refused before the first byte, the test included: a
// path past the 16-bit length (TooLarge at Where::Length, Step::Upload with an upload, else Install) and a battery
// whose slot does not fit (Where::BatteryPath, Step::Save), neither calling `options`' before(); a ROM, a battery or
// the two together past a 32-bit count (Where::Size, Step::Upload or Save) is refused the same way.
Status bootRom(Edio& edio, std::string_view path, ByteView rom, ByteView srm, Upload upload, std::uint16_t* mapIndex,
               Progress progress = {}, BootOptions options = {});

}  // namespace rp::n8
