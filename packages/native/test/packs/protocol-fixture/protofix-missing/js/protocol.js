// The protocol fixture's node bundle (docs/downloadable-packs-m2e-plan.md): global code appending definitions to the
// global the script host reads, in the shape defineNode makes, as tools/build-pack-halves.mjs writes a protocol half.
// defines only one of the two types its pack.json declares.
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
  define("rp.protofix-missing.up", [{ id: "in", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }],
    function () {
      return { process: function (b) {
        var ms = b.messages("in");
        for (var i = 0; i < ms.length; i++) {
          var d = ms[i].data;
          b.send("midiOut", ms[i].frame, [d[0], d[1] + 12, d[2]]);
        }
      } };
    });

})();
