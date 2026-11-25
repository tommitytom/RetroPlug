// The protocol fixture's node bundle (docs/downloadable-packs-m2e-plan.md): global code appending definitions to the
// global the script host reads, in the shape defineNode makes, as tools/build-pack-halves.mjs writes a protocol half.
// defines rp.midi.route, a built-in script node.
(function () {
  var nodes = (globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || []);
  function define(id, ports, create) {
    var layout = { ports: ports, params: [] };
    nodes.push({
      id: id,
      descriptor: function () { return { id: id, ports: ports, params: [], settings: { type: "object" } }; },
      parseSettings: function (raw) { return raw === undefined ? {} : raw; },
      layout: function () { return layout; },
      create: create,
      reshapes: false,
      faultOnThrow: false,
    });
  }
  define("rp.midi.route", [], function () { return { process: function () {} }; });

})();
