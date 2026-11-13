// The feature roles that have a protocol node, for a registry a test builds by hand: every one is its package's own now,
// in the order the app's list always registered them (mgb, nes-n8-midi, risa-sync, sms-sync, genmddj-sync, lsdj-sync). Same as
// test/_coreRoles.ts: a helper for the test tree, not an app module, so a test that needs "a registry with the feature
// roles" keeps one list while the carts' roles live in their packages. lsdj-assets, risa-assets and bliptoaster's two
// have no protocol node and a test registers them by name (registerLsdjAssetsRole, registerRisaAssetsRole,
// registerBlipToasterRole).
import type { RoleRegistry } from "@retroplug/core/systemRoles";
import { NES_N8_MIDI_ROLE } from "@retroplug/n8/app";
import { MGB_ROLE } from "@retroplug/mgb/app";
import { RISA_SYNC_ROLE } from "@retroplug/risa/risaRole";
import { SMS_SYNC_ROLE } from "@retroplug/smsggdj/app";
import { GENMDDJ_SYNC_ROLE } from "@retroplug/genmddj/app";
import { LSDJ_SYNC_ROLE } from "@retroplug/lsdj/app";

/** Register the built-in feature roles that have a protocol node, in the app's registration order. */
export function registerFeatureRoles(registry: RoleRegistry): void {
  registry.registerRole(MGB_ROLE);
  registry.registerRole(NES_N8_MIDI_ROLE);
  registry.registerRole(RISA_SYNC_ROLE);
  registry.registerRole(SMS_SYNC_ROLE);
  registry.registerRole(GENMDDJ_SYNC_ROLE);
  registry.registerRole(LSDJ_SYNC_ROLE);
}
