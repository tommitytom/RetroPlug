// Case transform for a prompt keystroke. DPF's "key" bus carries the UNSHIFTED code point (the A key is
// always 'a' regardless of Shift — @see Widget.hpp KeyboardEvent), plus the live modifier mask. This maps
// that (char, shift) pair to the character the field should receive, per the prompt's casing policy:
//   - "mixed" (default): Shift uppercases a letter, as a normal text field does.
//   - "upper": every letter is forced uppercase (LSDj / risa song names are uppercase-only).
// Non-letters are returned unchanged (Shift+digit symbols would need a keyboard-layout map we don't carry).

export type Casing = "mixed" | "upper";

export function applyCasing(ch: string, shift: boolean, casing?: Casing): string {
  if (casing === "upper") return ch.toUpperCase();
  return shift ? ch.toUpperCase() : ch;
}
