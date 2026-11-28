"use strict";
(() => {
  // ts/appRuntime.ts
  var grid = { rowCount: 0, channelCount: 4, hasContent: () => false };
  var model = {
    channelCount: 4,
    position: () => ({
      playing: false,
      channels: [0, 1, 2, 3].map(() => ({ playing: false, songRow: null }))
    }),
    grid: () => grid,
    launch: () => {
    },
    advance: () => {
    },
    stop: () => {
    },
    reset: () => {
    },
    setRowTicks: () => {
    },
    anchorTo: () => {
    }
  };
  var runtime = {
    id: "tsfix-proto-kind-app",
    factory: () => () => {
    },
    playbackModel: () => model,
    target: (send) => {
      send([176, 127, 42]);
      return { maxRow: 0, launch: () => {
      }, release: () => {
      } };
    }
  };
  var appRuntime_default = runtime;

  // kinds-entry.js
  globalThis.__rp_registerKindItems("controllerApps", [appRuntime_default], ["tsfix-proto-kind-app"]);
})();
