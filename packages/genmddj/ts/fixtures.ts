// Synthetic genmddj images for the TS tiers: enough of a Mega Drive header to carry the identity this package matches
// on, and nothing else. A case builds one here rather than vendoring a ROM, and the version's offset is a parameter so
// the identity is shown to be the scan, not one pinned position.

/** The version string's offset in the built ROM, the deepest place a real build is known to keep it. */
const GENMDDJ_VERSION_OFFSET = 0xd1b1;

/** A Mega Drive image whose header is a genmddj build's, and whose body carries `version` at `versionOffset`, which
 *  defaults to the built ROM's own $D1B1. A case can pass another offset, which is how the scan's "not position-pinned"
 *  claim is tested. */
export function genmddjHeader(
  version = "V0.27",
  productCode = "GM GENMDDJ-00",
  versionOffset = GENMDDJ_VERSION_OFFSET,
): Uint8Array {
  const rom = new Uint8Array(0x10000); // the size the provider is shown (providerHeaderBytes): $D1B1 fits inside it
  const set = (offset: number, text: string) => {
    for (let i = 0; i < text.length; i++) rom[offset + i] = text.charCodeAt(i);
  };
  set(0x100, "SEGA MEGA DRIVE ");
  set(0x180, productCode);
  set(versionOffset, version);
  return rom;
}
