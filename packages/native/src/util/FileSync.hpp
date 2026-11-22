#pragma once

#include <filesystem>
#include <string>

namespace rp {

// Force `path`'s already-written contents out of the OS page cache to the storage device, so a
// crash after this returns cannot resurrect a shorter, older, or zero-length version of the file.
//
// This is the half of "atomic write" that the rename alone does not give you. fs::rename is atomic
// with respect to the DIRECTORY - a reader sees the old file or the new one, never a mix - but it
// says nothing about whether the new file's BYTES reached the disk. Without this, a crash between
// the write and the flush leaves the rename durable and its contents not: the file is present,
// correctly named, and empty. Syncing the temp BEFORE the rename is what closes that window.
//
// Deliberately NOT a full durability barrier. This call does not sync the parent directory (so a
// crash may lose the rename itself and leave the previous file in place - which is a fine outcome,
// and the one the contract promises; the state.json writer, whose rename must outlast a crash, syncs
// the directory itself with fsyncDirectory), and macOS gets plain fsync rather than F_FULLFSYNC (which drains the
// drive's own write cache). Both are omitted on purpose: the SRAM auto-save mirror runs this every
// couple of seconds, and a per-tick drive barrier is not worth paying for a guarantee one step
// stronger than "never torn".
//
// Out of line because the Windows implementation needs <windows.h>, and the same rule applies here
// as to processAlive: one TU pays for that include instead of every caller. Returns false if the
// file cannot be opened or the flush fails.
bool fsyncPath(const std::string& path);
// The same for a path, which Windows opens by its wide name (CreateFileW), so a file under a profile folder outside
// the ANSI code page is flushed too.
bool fsyncPath(const std::filesystem::path& path);
// Spelled out because a string literal converts to both of the above equally well, which is ambiguous.
inline bool fsyncPath(const char* path) { return fsyncPath(std::string(path)); }
// Flush a directory's entries (a rename into it) to the device. POSIX only: Windows does not open directories for
// this, and its callers rename with MOVEFILE_WRITE_THROUGH instead, so there it answers true.
bool fsyncDirectory(const std::filesystem::path& dir);

} // namespace rp
