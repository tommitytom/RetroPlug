// The protocol node of tsfix-proto-kind: its ports are the ones pack.json wires (in, midiOut). Its process sends
// nothing.
(function () {
  var nodes = (globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || []);
  var id = "rp.tsfix-proto-kind.echo";
  var ports = [{ id: "in", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }];
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
