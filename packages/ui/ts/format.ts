// The UI's small text formatters, plain TypeScript: what a readout draws a byte as. The two debug overlays are plain
// TypeScript in their packages and the kit's DebugReadout is React, so the one copy lives here, where both sides can
// reach it without a cart importing the React kit for a string format.
/** A byte as two uppercase hex digits, "--" when absent: the readout cells' format. */
export const hex2 = (n: number | null): string => (n == null ? "--" : n.toString(16).toUpperCase().padStart(2, "0"));
