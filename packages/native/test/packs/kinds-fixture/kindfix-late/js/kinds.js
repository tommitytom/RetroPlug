// The kinds fixture's late variant: its app's target tries the registrar when the session builds it, which is after
// the Engine closed it, and sends CC 127 = 1 if the call threw, 0 if it did not.
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
    id: "kindfix-late-app",
    factory: function () { return function () {}; },
    playbackModel: function () { return model; },
    target: function (send) {
      var threw = false;
      try {
        globalThis.__rp_registerKindItems("controllerApps", [], []);
      } catch (e) {
        threw = true;
      }
      send([0xb0, 0x7f, threw ? 1 : 0]);
      return { maxRow: 0, launch: function () {}, release: function () {} };
    },
  };
  globalThis.__rp_registerKindItems("controllerApps", [app], ["kindfix-late-app"]);
})();
