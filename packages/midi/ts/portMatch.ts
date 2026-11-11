// Port-name matching (docs/modular-graph-phase-f5b-design.md section 6.3): a project saves the ports it opened, and a
// load has to find them again on this machine. Pure and platform-free, so the table of real ALSA, WinMM and CoreMIDI
// names beside it is the whole test.
//
// The rules are TIERS, tried in order, and each tier has to find exactly ONE present port:
//
//   1. the name EXACTLY, which is what a project that has only ever been opened here needs;
//   2. ALSA's trailing "<client>:<port>" numbers stripped ("LPProMK3 MIDI 24:0" against "... 28:0"), which change as
//      other clients come and go. This tier keeps the rest of the name, so a multi-port interface's "MIDI 1" and
//      "MIDI 2" still tell its two ports apart;
//   3. the index RtMidi appends to EVERY Windows WinMM name stripped ("MIDISPORT 2x2 Port A 1" against "... 2"), which
//      changes with the driver's enumeration order.
//
// Every tier folds runs of spaces and surrounding whitespace, and CoreMIDI's immediate repeat of the device name
// ("Launchpad Pro MK3 Launchpad Pro MK3" against "Launchpad Pro MK3"). The tiers are separate because one strip cannot
// serve both: "MIDI 1 24:0" and "Port A 1" both end in a number, and only the ALSA rule may keep the first one, or a
// two-port interface's ports would normalise to the same name and resolve to nothing.
//
// A tier that finds two candidates (two identical interfaces) or none resolves to nothing: the entry stays saved, shows
// "(missing)" and opens nothing, which is a state the user can fix by picking the port again - guessing between two
// devices would silently drive the wrong one.
export function resolvePort(saved: string, present: readonly string[]): string | null {
  if (!saved) return null;
  if (present.includes(saved)) return saved;
  for (const strip of STRIPS) {
    const want = foldPortName(strip(saved));
    if (!want) return null;
    const matches = present.filter((name) => foldPortName(strip(name)) === want);
    if (matches.length === 1) return matches[0];
    if (matches.length > 1) return null; // two candidates: the next tier would guess between them
  }
  return null;
}

/** One stripping rule: what each tier cuts off a name's end before the shared fold. The ALSA rule runs first and is the
 *  narrower one, so a name it resolves exactly is never handed to the index rule. */
const STRIPS: readonly ((name: string) => string)[] = [
  // ALSA's client:port suffix, at the end of the name and nowhere else.
  (name) => name.trim().replace(/\s+\d+:\d+$/, ""),
  // RtMidi's WinMM "<name> <index>" suffix (deps/rtmidi appends the port number to every Windows name).
  (name) => name.trim().replace(/\s+\d+$/, ""),
];

/** A name with the machine's punctuation folded away: runs of spaces, and CoreMIDI's immediate repeat of the whole
 *  device name ("Device Device", "Device Device Port A"), which a port that carries its device's name lists. */
export function foldPortName(name: string): string {
  let s = name.trim().replace(/\s+/g, " ");
  for (let i = 0; i < 2; i++) {
    const words = s.split(" ");
    const n = words.length;
    if (n >= 2 && n % 2 === 0 && words.slice(0, n / 2).join(" ") === words.slice(n / 2).join(" ")) {
      s = words.slice(0, n / 2).join(" ");
    }
  }
  return s;
}

/** A project's saved entries resolved against a fresh listing: the EXACT names to hand the host, in the saved order.
 *  A name that resolves to nothing is left out (it stays in the section, and the row shows it as missing). */
export function resolvePorts(saved: readonly string[], present: readonly string[]): string[] {
  const out: string[] = [];
  for (const name of saved) {
    const hit = resolvePort(name, present);
    if (hit && !out.includes(hit)) out.push(hit);
  }
  return out;
}
