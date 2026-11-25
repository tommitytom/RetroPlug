// A protocol node whose descriptor ports (midi, midiOut) are not the ports its pack.json wires (in): the TS loader
// refuses it (docs/downloadable-packs-m2e-plan.md). Its process sends nothing.
(function () {
  var nodes = (globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || []);
  var id = "rp.tsfix-proto-ports.echo";
  var ports = [{ id: "midi", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }];
  var layout = { ports: ports, params: [] };
  nodes.push({
    id: id,
    descriptor: function () { return { id: id, ports: ports, params: [], settings: { type: "object" } }; },
    parseSettings: function (raw) { return raw === undefined ? {} : raw; },
    layout: function () { return layout; },
    create: function () { return { process: function () {} }; },
    reshapes: false,
    faultOnThrow: false,
  });
})();
