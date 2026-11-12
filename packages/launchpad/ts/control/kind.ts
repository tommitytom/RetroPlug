// The controllerApps contribution kind's control-plane half (docs/modular-graph-phase-f5a-design.md section 6.5): what
// an app IS to the app's registry, and the checks only this package can make over the data a contributing manifest
// carries.
//
// The kind has two jobs. Its `accept` fills this package's ControllerRegistry from the control-plane halves the
// contributing packages' app modules list, and keeps the manifests' data (each app's `syncOverride`) as the MENU's
// table. Its `check` validates that data before anything is accepted: an override that is not an object, a misspelt
// key, a missing node or mode, an `off` that changes nothing, or a node type no package in this product wires would
// otherwise plan and do nothing.
//
// The data itself stays untyped (KindData): a shell module builds it from the manifests without importing this package,
// which is what lets the planner place an override with no app and no zod in its bundle.
import { defineKind, type KindCheckContext, type KindData } from "@retroplug/core/defineKind";
import { controllerApps, type ControllerAppContribution } from "../registry";
import { setControllerSyncData } from "./placer";

/** The sync override's fields: the node type it applies to, the kind of the role that node belongs to (what a menu
 *  asks a view's placed kinds), the mode it sets, the polled SYNC value that counts as the
 *  cart listening, the mode a cart that is not listening is put in, and the name of that node's setting that divides
 *  the clock it sends the cart. */
const SYNC_KEYS = ["node", "role", "mode", "listening", "off", "divisor"];

/** One app's `syncOverride`, checked: the data a placer applies and a menu reads. */
function checkSyncOverride(appId: string, sync: unknown, ctx: KindCheckContext): void {
  const where = `the controller app ${appId}'s syncOverride`;
  if (sync === null || typeof sync !== "object" || Array.isArray(sync)) {
    throw new Error(`${where} must be an object, not ${JSON.stringify(sync)}`);
  }
  const rule = sync as Record<string, unknown>;
  for (const key of Object.keys(rule)) {
    if (!SYNC_KEYS.includes(key)) {
      throw new Error(
        `${where} has ${JSON.stringify(key)}, which nothing reads (its keys are ${SYNC_KEYS.join(", ")})`,
      );
    }
  }
  if (typeof rule.node !== "string" || rule.node === "") {
    throw new Error(`${where} needs a "node": the cart sync node type its mode is set on`);
  }
  if (rule.role !== undefined && (typeof rule.role !== "string" || rule.role === "")) {
    throw new Error(
      `${where}'s "role" must be the kind of the role that node belongs to, not ${JSON.stringify(rule.role)}`,
    );
  }
  if (typeof rule.mode !== "string" || rule.mode === "") {
    throw new Error(`${where} needs a "mode": the mode that node is put in`);
  }
  // The node type a mode is set on has to be one this product WIRES: a misspelt one, or one no placer places, would be
  // written onto a node that is not there and do nothing. Only the product knows its wired types, so the generated
  // registry hands them in; the check runs before the `listening` return below, or an override without a listening
  // value would never reach it.
  if (!ctx.nodeTypes.includes(rule.node)) {
    throw new Error(`${where} names ${JSON.stringify(rule.node)}, which no package declares a protocol node for`);
  }
  // A divisor is read off the node's own config by name (the placer hands the session what it finds there), so it is a
  // setting's name or nothing: a number here would be read as no setting and leave the session walking every tick.
  // That the name is one of the node's settings is not checked here (the kind sees the node's type and no schema); a
  // misspelt one reads as 1 at the placer. What holds the real name is the exact assertion on lsdj-launchpad's
  // KIND_DATA in build-graph.test.ts and store-graph.test.ts's end-to-end case, where a changed divisor reaches the
  // session.
  if (rule.divisor !== undefined && (typeof rule.divisor !== "string" || rule.divisor === "")) {
    throw new Error(
      `${where}'s "divisor" must name the setting that divides that node's 24-tick clock, not ` +
        JSON.stringify(rule.divisor),
    );
  }
  if (rule.listening === undefined) {
    if (rule.off !== undefined) {
      throw new Error(`${where} names an "off" mode and no "listening" value, so the mode it is for is never used`);
    }
    return;
  }
  if (typeof rule.listening !== "string" || rule.listening === "") {
    throw new Error(
      `${where}'s "listening" must be the polled SYNC value that counts as listening, not ` +
        JSON.stringify(rule.listening),
    );
  }
  if (typeof rule.off !== "string" || rule.off === "") {
    throw new Error(
      `${where} needs an "off": the mode a cart polled at anything but ${JSON.stringify(rule.listening)} is put in`,
    );
  }
  if (rule.off === rule.mode) {
    throw new Error(
      `${where} names ${JSON.stringify(rule.mode)} as both its mode and the mode a cart that is not listening is put ` +
        "in, so the override changes nothing",
    );
  }
}

/** The controllerApps kind: the registry's items, the menu's sync rules, and the checks over the data. */
export const controllerAppsKind = defineKind<ControllerAppContribution>({
  accept(items: readonly ControllerAppContribution[], data: KindData): void {
    for (const item of items) controllerApps.register(item);
    setControllerSyncData(data);
  },
  check(items: readonly ControllerAppContribution[], data: KindData, ctx: KindCheckContext): void {
    for (const item of items) {
      const entry = Object.prototype.hasOwnProperty.call(data, item.id) ? data[item.id] : {};
      if (entry.syncOverride !== undefined) checkSyncOverride(item.id, entry.syncOverride, ctx);
    }
  },
});

export default controllerAppsKind;
