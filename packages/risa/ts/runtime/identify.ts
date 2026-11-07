// Identify a risa ROM's app version by scanning the PRG for the ASCII "RISA V<major>.<minor>.<patch>"
// marker (as risa's own tools/rom_patcher/src/rom.js does). NES ROMs carry no version field, and the
// marker isn't at a fixed offset, so this scans — used by the runtime overlay (which has the whole ROM
// via readFile), NOT the 336-byte-header ROM provider. Returns e.g. "2.2.1", or null if not found.
const MARKER = [0x52, 0x49, 0x53, 0x41, 0x20, 0x56]; // "RISA V"

export function identifyRisaVersion(rom: Uint8Array): string | null {
  const end = rom.length - MARKER.length;
  for (let i = 0x10; i < end; i++) {
    let hit = true;
    for (let j = 0; j < MARKER.length; j++) {
      if (rom[i + j] !== MARKER[j]) {
        hit = false;
        break;
      }
    }
    if (!hit) continue;
    let s = "";
    for (let k = i + MARKER.length; k < rom.length && s.length < 12; k++) {
      const c = rom[k];
      if ((c >= 0x30 && c <= 0x39) || c === 0x2e) s += String.fromCharCode(c); // [0-9.]
      else break;
    }
    const m = s.match(/^(\d+\.\d+\.\d+)/);
    if (m) return m[1];
  }
  return null;
}
