// The N8 device-info decoder (src/n8/sysInfo.ts): the 64-byte CMD_SYS_INF block + 8-byte CMD_GET_VDC block
// -> structured identity + voltages. Pure, no hardware. Hand-builds a reply with known fields at edlink's
// offsets and asserts the decode (serial, form factor, versions, FAT dates, flash, voltages).
import { test, expect } from "../../testing/harness";
import { decodeSysInfo, decodeVdc, fatDateStr, vdcStr, DEV_ID_N8_PRO } from "../../src/n8/sysInfo";

function sysReply(): Uint8Array {
  const b = new Uint8Array(64);
  const put32 = (o: number, v: number): void => {
    b[o] = v & 0xff; b[o + 1] = (v >> 8) & 0xff; b[o + 2] = (v >> 16) & 0xff; b[o + 3] = (v >>> 24) & 0xff;
  };
  const put16 = (o: number, v: number): void => { b[o] = v & 0xff; b[o + 1] = (v >> 8) & 0xff; };
  put32(20, 0x12345678); // serial_g
  put32(24, 0x9abcdef0); // serial_l
  put32(28, 100); // boot_ctr
  put32(32, 250); // game_ctr
  put16(36, 22223); // asm_date = FAT 2023-06-15
  put16(40, 0x0102); // sw_ver
  put16(42, 0x0304); // hw_ver
  put16(44, 0x0210); // boot_ver
  b[46] = DEV_ID_N8_PRO; // device_id 0x17
  b[52] = 1; // CartForm 1 = Famicom
  b[55] = 24; // flash shift -> 1<<24 = 16 MB
  put16(56, 22580); // sw_date = FAT 2024-01-20
  return b;
}

test("decodeSysInfo decodes serial, versions, form factor, dates, and flash", () => {
  const s = decodeSysInfo(sysReply());
  expect(s.serial).toBe("12345678.9ABCDEF0");
  expect(s.deviceId).toBe(0x17);
  expect(s.deviceName).toBe("EverDrive-N8 PRO");
  expect(s.formFactor).toBe("Famicom");
  expect(s.bootVer).toBe(0x0210);
  expect(s.swVer).toBe(0x0102);
  expect(s.hwVer).toBe(0x0304);
  expect(s.flashSizeBytes).toBe(16 * 0x100000);
  expect(fatDateStr(s.buildDate)).toBe("2023-06-15");
  expect(fatDateStr(s.coreDate)).toBe("2024-01-20");
  expect(s.gameCtr).toBe(250);
  expect(s.bootCtr).toBe(100);
});

test("decodeSysInfo maps an unknown device_id / form factor", () => {
  const b = sysReply();
  b[46] = 0x99;
  b[52] = 5;
  const s = decodeSysInfo(b);
  expect(s.deviceName).toBe("unknown device (0x99)");
  expect(s.formFactor).toBe("unknown (5)");
});

test("decodeVdc reads four u16 voltages and vdcStr formats them like edlink", () => {
  const v = new Uint8Array([0x00, 0x05, 0x50, 0x02, 0x20, 0x01, 0x01, 0x03]);
  const d = decodeVdc(v);
  expect(d.v50).toBe(0x0500);
  expect(d.v25).toBe(0x0250);
  expect(vdcStr(d.v50)).toBe("05.00");
  expect(vdcStr(d.v25)).toBe("02.50");
  expect(vdcStr(d.bat)).toBe("03.01");
});

test("decoders reject short replies", () => {
  expect(() => decodeSysInfo(new Uint8Array(40))).toThrow();
  expect(() => decodeVdc(new Uint8Array(4))).toThrow();
});
