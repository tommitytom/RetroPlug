// Decodes the N8's device-info replies over USB: Edio.sysInfo() (a fixed 64-byte block) and Edio.vdc() (8
// bytes) into structured identity + board voltages. Wire layout + decoders ported from edlink
// DEV_EDN8/DeviceIO.getSysInf/getCartForm + Tools; all multi-byte fields are little-endian. Pure +
// host-agnostic like sniffer.ts (decoding, not protocol - no C++ twin). The 64-byte reply's first 20 bytes
// are reserved; only edlink's documented offsets are parsed.

const u16 = (b: Uint8Array, o: number): number => b[o] | (b[o + 1] << 8);
const u32 = (b: Uint8Array, o: number): number => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const hex = (v: number, n: number): string => v.toString(16).toUpperCase().padStart(n, "0");

export const DEV_ID_N8_PRO = 0x17;

/** A FAT-encoded date (asm_date / sw_date): the N8 stores its firmware build dates this way. */
export interface FatDate {
  day: number;
  month: number;
  year: number;
}

export interface N8SysInfo {
  serial: string; // "GGGGGGGG.LLLLLLLL" (hex)
  deviceId: number;
  deviceName: string; // "EverDrive-N8 PRO" for device_id 0x17
  formFactor: string; // "NES" | "Famicom" | "unknown (n)"
  bootVer: number; // bootloader version (raw u16)
  swVer: number; // software version
  hwVer: number; // hardware version
  flashSizeBytes: number; // 1 << shift
  buildDate: FatDate; // asm_date: firmware assemble date
  coreDate: FatDate; // sw_date: MCU-core build date (edlink's "mcu core" version)
  gameCtr: number; // lifetime games launched
  bootCtr: number; // lifetime boots
}

const fat = (ts: number): FatDate => ({ day: ts & 31, month: (ts >> 5) & 15, year: 1980 + (ts >> 9) });

/** "YYYY-MM-DD" for a FatDate. */
export const fatDateStr = (d: FatDate): string =>
  `${d.year}-${String(d.month).padStart(2, "0")}-${String(d.day).padStart(2, "0")}`;

/** Decode the 64-byte CMD_SYS_INF reply (offsets verbatim from edlink getSysInf; little-endian). */
export function decodeSysInfo(b: Uint8Array): N8SysInfo {
  if (b.length < 64) throw new Error(`sysInfo reply too short: ${b.length} < 64`);
  const deviceId = b[46];
  const form = b[52];
  return {
    serial: `${hex(u32(b, 20), 8)}.${hex(u32(b, 24), 8)}`,
    deviceId,
    deviceName: deviceId === DEV_ID_N8_PRO ? "EverDrive-N8 PRO" : `unknown device (0x${hex(deviceId, 2)})`,
    formFactor: form === 0 ? "NES" : form === 1 ? "Famicom" : `unknown (${form})`,
    bootVer: u16(b, 44),
    swVer: u16(b, 40),
    hwVer: u16(b, 42),
    flashSizeBytes: 1 << b[55],
    buildDate: fat(u16(b, 36)),
    coreDate: fat(u16(b, 56)),
    gameCtr: u32(b, 32),
    bootCtr: u32(b, 28),
  };
}

export interface N8Vdc {
  v50: number;
  v25: number;
  v12: number;
  bat: number;
}

/** Decode the 8-byte CMD_GET_VDC reply (four little-endian u16). Use vdcStr to format each as volts. */
export function decodeVdc(b: Uint8Array): N8Vdc {
  if (b.length < 8) throw new Error(`vdc reply too short: ${b.length} < 8`);
  return { v50: u16(b, 0), v25: u16(b, 2), v12: u16(b, 4), bat: u16(b, 6) };
}

/** Format a raw vdc u16 the way edlink's devinf does: whole.fraction in hex (0x0500 -> "05.00"). */
export const vdcStr = (v: number): string => `${hex(v >> 8, 2)}.${hex(v & 0xff, 2)}`;
