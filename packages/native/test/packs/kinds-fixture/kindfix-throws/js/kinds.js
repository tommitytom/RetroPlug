// The kinds fixture's throwing variant: its bundle throws before it registers anything.
(function () {
  throw new Error("the kinds fixture throws");
  var grid = { rowCount: 0, channelCount: 4, hasContent: function () { return false; } };
  var model = {
    channelCount: 4,
    position: function () {
      return { playing: false, channels: [0, 1, 2, 3].map(function () { return { playing: false, songRow: null }; }) };
    },
    grid: function () { return grid; },
    launch: function () {}, advance: function () {}, stop: function () {}, reset: function () {},
    setRowTicks: function () {}, anchorTo: function () {},
  };
  var app = {
    id: "kindfix-throws-app",
    factory: function () { return function () {}; },
    playbackModel: function () { return model; },
    target: function (send) {
      send([0xb0, 0x7f, 0x2a]);
      return { maxRow: 0, launch: function () {}, release: function () {} };
    },
  };
  globalThis.__rp_registerKindItems("controllerApps", [app], ["kindfix-throws-app"]);
})();
