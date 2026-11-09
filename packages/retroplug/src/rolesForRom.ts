// The roles a ROM gets, in ONE place: the header the shell reads for the ROM providers, the providers' own role
// suggestions, and the config fields a role derives from the FILE rather than from the user's choices
// (docs/modular-graph-phase-f4a-design.md section 4.5). A fresh system's build (SystemsStore.defaultRoles) and a render's
// classification of the ROM it is about to build (render/render.ts resolveRenderRom) both come through here, so a render
// asks the integration the built system will have instead of keeping a second copy of the read.
//
// The read is in TIERS, because two cases need more than a header and neither is the common one:
//
//  - **Sega always.** No title field, and the header itself sits at the END of a bank. smsggdj's marker is at $3640, so
//    a ROLE_HEADER_LEN read would leave the provider nothing to match on and force `sms-sync` onto every SMS cart -
//    which it must not, since it drives Player 2's button lines. The console's descriptor says how much: its
//    `providerHeaderBytes`.
//  - **A role that derives a fact from deeper in the file.** BlipToaster's chip is named in the SIG block for a tagged
//    ROM (inside the short header) and only in the on-screen label inside the PRG region for an older one, so the role's
//    `deriveConfig.headerBytes` asks for the deeper read exactly then. A role that asks for nothing pays nothing.
import { ROLE_HEADER_LEN, type RoleInstance, type RoleRegistry } from "@retroplug/core/systemRoles";
import { providerHeaderBytes, type Core, type Platform } from "./consoles";

/** A reader of a file's first `length` bytes: the backend's readFilePrefix, narrowed to what the roles need. */
export interface RoleHeaderReader {
  readFilePrefix(path: string, length: number): Uint8Array | null;
}

/** What a ROM build asks of the roles: its console, the core that runs it, and where its bytes are. `embeddedRom` is the
 *  baked-in marker ("" for a file-backed ROM), the one signal an embedded ROM has, since its bytes never reach TS. */
export interface RomRoles {
  readonly core: Core;
  readonly platform: Platform;
  readonly romPath: string;
  readonly embeddedRom?: string;
}

function readHeader(reader: RoleHeaderReader, romPath: string, length: number): Uint8Array {
  return reader.readFilePrefix(romPath, length) ?? new Uint8Array();
}

/** The roles a fresh system built from `rom` gets: the core-config role, then every provider's feature roles for the
 *  header it is shown, with each role's ROM-derived config fields applied. Empty for a ROM with no file to read (an
 *  embedded one), which is what its providers are told. */
export function rolesForRom(registry: RoleRegistry, reader: RoleHeaderReader, rom: RomRoles): RoleInstance[] {
  const embeddedRom = rom.embeddedRom ?? "";
  const header =
    rom.romPath && !embeddedRom
      ? readHeader(reader, rom.romPath, providerHeaderBytes(rom.platform) ?? ROLE_HEADER_LEN)
      : new Uint8Array();
  const roles = registry.defaultRoles(rom.core, rom.platform, header, embeddedRom);
  return derivedRoleConfigs(registry, reader, roles, rom, header);
}

/** `roles` with each role's ROM-derived config fields applied, for a role list that is already known (a loaded
 *  project's, which was stored). `header` is a read the caller already made, when it made one. */
export function derivedRoleConfigs(
  registry: RoleRegistry,
  reader: RoleHeaderReader,
  roles: readonly RoleInstance[],
  rom: Pick<RomRoles, "romPath" | "embeddedRom">,
  header?: Uint8Array,
): RoleInstance[] {
  const embeddedRom = rom.embeddedRom ?? "";
  if (!rom.romPath || embeddedRom) return [...roles];
  const deriving = roles.flatMap((role) => {
    const derive = registry.roleType(role.kind)?.deriveConfig;
    return derive ? [derive] : [];
  });
  if (!deriving.length) return [...roles];

  // An unreadable file derives nothing: the roles keep the config they carry, as they always have. A role that
  // derives from zero bytes would otherwise be asked to write a "fact" about nothing.
  let bytes = header ?? readHeader(reader, rom.romPath, ROLE_HEADER_LEN);
  if (!bytes.length) return [...roles];
  // The short header is what every role's `headerBytes` is asked about; a role that needs more asks for it, and the
  // deepest answer is read ONCE for all of them.
  const short = bytes.length > ROLE_HEADER_LEN ? bytes.subarray(0, ROLE_HEADER_LEN) : bytes;
  let need = short.length;
  for (const derive of deriving) need = Math.max(need, derive.headerBytes(short));
  if (need > bytes.length) bytes = readHeader(reader, rom.romPath, need);

  return roles.map((role) => {
    const derive = registry.roleType(role.kind)?.deriveConfig;
    return derive ? { ...role, config: derive.derive(bytes, role.config ?? {}) } : role;
  });
}
