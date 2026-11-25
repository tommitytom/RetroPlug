// The kinds fixture's runtime half (docs/downloadable-packs-m2f-plan.md): global code, as tools/build-pack-halves.mjs
// writes a kinds half, registering one item through the built-in bundle's registrar. Its target sends CC 127 = 42
// once, when the session builds it, so a document driving the session out of the host sees this code ran.
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
    id: "kindfix-app",
    factory: function () { return function () {}; },
    playbackModel: function () { return model; },
    target: function (send) {
      send([0xb0, 0x7f, 0x2a]);
      return { maxRow: 0, launch: function () {}, release: function () {} };
    },
  };
  globalThis.__rp_registerKindItems("controllerApps", [app], ["kindfix-app"]);
})();
