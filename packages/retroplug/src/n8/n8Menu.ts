// The on-device Everdrive N8 menu command channel - a faithful TS port of native N8Menu (N8Menu.{hpp,cpp}).
// While the N8's file-browser menu is running it reads '*'-prefixed commands from the same cart FIFO the
// MIDI bridge writes to, and replies over USB. This drives the menu to install + boot a ROM: the menu
// firmware itself parses the iNES header, sources the standard mapper core from its SD, and boots the FPGA -
// the host does none of that. Only valid while the MENU is running; after appStart() the game runs (to
// switch ROMs, reset() back to the menu first).

import { Edio } from "./edio";

const hex2 = (v: number): string => v.toString(16).padStart(2, "0");

export class N8Menu {
  constructor(private readonly edio: Edio) {}

  // Handshake with the menu: '*t' -> expect 'k'. Throws if the menu isn't running / doesn't answer.
  test(): void {
    this.cmd("t");
    const resp = this.edio.rx8(); // throws on timeout (menu not running / not listening)
    if (resp !== 0x6b /* 'k' */)
      throw new Error(`N8 menu: unexpected test response 0x${hex2(resp)} (is the menu running?)`);
  }

  // Select a ROM by its device (SD) path: '*n' + length-prefixed path -> status(0=ok) -> 16-bit map index.
  // The menu loads the ROM (iNES parse, PRG/CHR, mapper core from SD, MapConfig). Returns the map index.
  appInstall(devicePath: string): number {
    this.edio.setReadTimeout(10000); // the menu loads the ROM + FPGA core from SD before replying
    this.cmd("n");
    this.edio.fifoTxString(devicePath);
    const status = this.edio.rx8(); // game-select status
    if (status === 0x44) // ERR_OUT_OF_MEMORY: the menu heap is dirty (usually after a prior failed load)
      throw new Error(
        "N8 menu out of memory (0x44) - the menu heap is dirty (e.g. after a prior failed load). " +
          "Power-cycle the console to a fresh menu and retry.",
      );
    if (status !== 0)
      throw new Error(`N8 menu: app install error 0x${hex2(status)} (path '${devicePath}')`);
    return this.edio.rx16(); // map index
  }

  // Boot the installed ROM: '*s'. The menu core drops out and the game runs.
  appStart(): void {
    this.cmd("s");
  }

  // Reboot back to the menu: '*r' -> best-effort ack. The console then reboots (~seconds, screen black), so
  // the caller must poll test() to know when the menu is actually back. Use to switch ROMs.
  reset(): void {
    this.edio.flushInput();
    this.cmd("r");
    this.edio.setReadTimeout(2000);
    try {
      this.edio.rx8();
    } catch {
      // no ack / already rebooting - fine; readiness is confirmed by the caller polling test()
    }
  }

  private cmd(c: string): void {
    this.edio.fifoWR(new Uint8Array([0x2a /* '*' */, c.charCodeAt(0)]));
  }
}
