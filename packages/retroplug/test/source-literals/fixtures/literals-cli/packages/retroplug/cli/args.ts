// A help line names a ROM by its extensions; a flag's value is a path. Only the path is a value a program compares, and
// only the path ends in a guarded extension with no whitespace before it.
export const HELP = "Render a ROM (.nes/.gb/.gba) or a .sav to a WAV file";

export const DEFAULT_ROM = "roms/cart.nes";

export const flagValue = (args: string[]): string => args[0] ?? DEFAULT_ROM;
