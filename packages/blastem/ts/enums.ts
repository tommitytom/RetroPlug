// The Mega Drive's settings labels. A native enum knob's labels are its ENUMERATOR NAMES with the first letter
// lowercased (SystemNode.hpp's knobs::Label<E>), so MdRegion's {Auto, NtscUs, NtscJp, Pal} spell exactly these four
// words and this tuple is their TS side; a hyphenated "ntsc-us" is refused by the native settings parser.
export const REGION_VALUES = ["auto", "ntscUs", "ntscJp", "pal"] as const;
