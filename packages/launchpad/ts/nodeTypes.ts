// The launchpad pack's node type ids, in a module of their own: the session script node's and the two surface nodes'
// (native nodes of the pack, which no manifest entry declares, since they are not script nodes). Apart from the modules
// that define and place them because the graph placer imports the ids alone: a placer's module is reachable from the
// planner's bundle, which carries no node code (test/script/bundle-protocol.test.ts).

/** rp.controller.session: the session script node (ts/controllerSession.ts). */
export const CONTROLLER_SESSION_NODE_TYPE = "rp.controller.session";

/** rp.launchpad.surface-in: the surface's traffic into the session, and its attached and dropped counts. */
export const SURFACE_IN_NODE_TYPE = "rp.launchpad.surface-in";

/** rp.launchpad.surface-out: the session's LED bytes back to the device. */
export const SURFACE_OUT_NODE_TYPE = "rp.launchpad.surface-out";
