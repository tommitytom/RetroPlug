// The Game Boy cartridge title field (0x134..0x143), decoded the way a ROM provider matches a cart by its title: to
// uppercase ASCII, stopping at the first NUL. A provider reads the ROM's header prefix, never the whole ROM, so this is
// all a headless cart identity is.
//
// It is deliberately not @retroplug/lsdj's runtime/identify's romTitle: that one stops at the first non-printable byte
// (a 15-character title runs up to 0x142, leaving 0x143's CGB flag, which must not reach a version parse), where a
// prefix match reads through it. Two decoders, each for the one question it answers.

/** The cartridge title, uppercased, up to the first NUL. */
export function cartridgeTitle(header: Uint8Array): string {
  let s = "";
  for (let i = 0x134; i < 0x144 && i < header.length; i++) {
    const c = header[i];
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s.toUpperCase();
}
