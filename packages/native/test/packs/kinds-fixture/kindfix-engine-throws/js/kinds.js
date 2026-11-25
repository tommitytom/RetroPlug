// The kinds fixture's engine-throws variant: it registers its item, then throws unless the table's trial prelude ran, so
// it loads in the trial and fails in an Engine.
(function () {
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
    id: "kindfix-engine-throws-app",
    factory: function () { return function () {}; },
    playbackModel: function () { return model; },
    target: function (send) {
      send([0xb0, 0x7f, 0]);
      return { maxRow: 0, launch: function () {}, release: function () {} };
    },
  };
  globalThis.__rp_registerKindItems("controllerApps", [app], ["kindfix-engine-throws-app"]);
  if (!globalThis.__rpKindsTrialCalls) throw new Error("the kinds fixture throws outside the trial");
})();
