// The two orthogonal axes a loaded system carries, as the role contract names them: what a ROM targets, and the
// emulator that runs it. They live here because ConstructSpec and RomContext name them, and a package may import neither
// the shell nor another package. The shell's platform.ts keeps the detection, the console-to-node table and the battery
// rules, which are the app's, and re-exports these two types from here.
export type Platform = "gb" | "nes" | "gba" | "sms" | "gg";
export type Core = "sameboy" | "mesen";
