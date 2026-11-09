// The battery-signature half of a tracker integration (docs/modular-graph-phase-f4a-design.md section 4.3): how a cart
// tells a MEANINGFUL change of its battery from one its own code makes every frame. The loose-`.sav` auto-save asks
// every integration in product order and takes the first answer, then falls back to a whole-image hash, so the rule is
// the image's and never a role's: it never looked at a system, only at the bytes.
//
// A cart answers null for an image that is not its own (LSDj: a full 128 KiB image with its 'jk' magic), so the next
// integration and then the hash get their turn.
export type SramSignature = (bytes: Uint8Array) => number | null;
