#pragma once

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
// Deliberately NOT a full durability barrier. The parent directory is not synced (so a crash may
// lose the rename itself and leave the previous file in place - which is a fine outcome, and the
// one the contract promises), and macOS gets plain fsync rather than F_FULLFSYNC (which drains the
// drive's own write cache). Both are omitted on purpose: the SRAM auto-save mirror runs this every
// couple of seconds, and a per-tick drive barrier is not worth paying for a guarantee one step
// stronger than "never torn".
//
// Out of line because the Windows implementation needs <windows.h>, and the same rule applies here
// as to processAlive: one TU pays for that include instead of every caller. Returns false if the
// file cannot be opened or the flush fails.
bool fsyncPath(const std::string& path);

} // namespace rp
