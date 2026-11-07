// risa's ROM provider: a risa cart gets the `risa` marker role that gates the Songs menu and the `risa-assets` role
// holding its non-destructive theme/font/kit overrides, plus - on a sync-capable build - the `risa-sync` role that
// drives its N8-FIFO host-sync receive path. Detected by the iNES 2.0 header fingerprint (MMC5 + 64 KB PRG-NVRAM),
// since NES ROMs carry no title field; the "RISAxyz" marker in the header prefix says the build is sync-capable, and
// older risa builds without it are unaffected.
import type { RoleInstance, RomProvider } from "@retroplug/core/systemRoles";
import { isRisaRomHeader, isRisaSyncRom } from "./romDetect";

export const RISA_ROM_PROVIDER: RomProvider = (rom): RoleInstance[] => {
  if (rom.platform !== "nes" || !isRisaRomHeader(rom.header)) return [];
  const roles: RoleInstance[] = [{ kind: "risa", config: {} }, { kind: "risa-assets", config: { overrides: [] } }];
  if (isRisaSyncRom(rom.header)) roles.push({ kind: "risa-sync", config: {} });
  return roles;
};
