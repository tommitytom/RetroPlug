// The kinds fixture pack's controller app runtime (docs/downloadable-packs-m2f-plan.md): an app that does nothing, whose
// target sends CC 127 = 42 once when the session builds it, so a test driving the session out of the host sees the
// pack's runtime ran in the built-in script contexts.
import type { ControllerAppRuntime, ControllerPlaybackModel } from "@retroplug/launchpad/apps";

const grid = { rowCount: 0, channelCount: 4, hasContent: () => false };

// Every member the interface asks for, each doing nothing; the position is idle.
const model: ControllerPlaybackModel = {
  channelCount: 4,
  position: () => ({
    playing: false,
    channels: [0, 1, 2, 3].map(() => ({ playing: false, songRow: null })),
  }),
  grid: () => grid,
  launch: () => {},
  advance: () => {},
  stop: () => {},
  reset: () => {},
  setRowTicks: () => {},
  anchorTo: () => {},
};

const runtime: ControllerAppRuntime = {
  id: "tsfix-app",
  factory: () => () => {},
  playbackModel: () => model,
  target: (send) => {
    send([0xb0, 0x7f, 0x2a]);
    return { maxRow: 0, launch: () => {}, release: () => {} };
  },
};

export default runtime;
