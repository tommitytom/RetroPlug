// risa's addition to the shared kit-bank format (which is @retroplug/tracker's nes/kitBank): the three rows of the
// ROM's resident kit-metadata MIRROR that a bank splice has to rewrite (risa's rom_upgrade.js
// updateRomKitMetadataFromBank). The mirror is risa's own - BlipToaster's ROM reads the kit index directly and carries
// no copy - so this is the one piece of kit-bank code that is not a format.

import {
  KIT_NAME_OFFSET,
  KIT_NAME_SIZE,
  KIT_SAMPLE_NAMES,
  KIT_SAMPLE_NAME_LEN,
  KIT_INDEX_OFFSET,
  KIT_INDEX_ENTRY,
  KIT_SLOT_COUNT,
  KIT_SLOT_EMPTY,
} from "@retroplug/tracker/nes/constants";
import { isBankPopulated } from "@retroplug/tracker/nes/kitBank";

/** Derive the three metadata-mirror rows for a kit slot from a bank's own bytes (no model needed) —
 *  risa's rom_upgrade.js updateRomKitMetadataFromBank. Used so a bank splice can update the mirror. */
export function deriveMetaFromBank(bank: Uint8Array): {
  nameBytes: Uint8Array;
  sampleNamesBytes: Uint8Array;
  slotPresentBytes: Uint8Array;
} {
  const populated = isBankPopulated(bank);
  const nameBytes = populated ? bank.slice(KIT_NAME_OFFSET, KIT_NAME_OFFSET + KIT_NAME_SIZE) : new Uint8Array(KIT_NAME_SIZE);
  const sampleNamesBytes = populated
    ? bank.slice(KIT_SAMPLE_NAMES, KIT_SAMPLE_NAMES + KIT_SLOT_COUNT * KIT_SAMPLE_NAME_LEN)
    : new Uint8Array(KIT_SLOT_COUNT * KIT_SAMPLE_NAME_LEN).fill(0x20);
  const slotPresentBytes = new Uint8Array(KIT_SLOT_COUNT);
  if (populated) {
    for (let slot = 0; slot < KIT_SLOT_COUNT; slot++) {
      slotPresentBytes[slot] = bank[KIT_INDEX_OFFSET + slot * KIT_INDEX_ENTRY] === KIT_SLOT_EMPTY ? 0 : 1;
    }
  }
  return { nameBytes, sampleNamesBytes, slotPresentBytes };
}
