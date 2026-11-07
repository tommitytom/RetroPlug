// The fixture core's barrel: enough of the node and role types for the generated modules' type imports.
export interface RoleType {
  readonly kind: string;
}
export type RomProvider = (rom: unknown) => { kind: string; config: Record<string, unknown> }[];
