// The graph document's endpoint rule, shared with C++: node ids carry no ".", so an endpoint splits at
// its first dot and a port id may itself contain dots ("pins.pulse").
import { test, expect } from "../../testing/harness";
import { endpoint, parseEndpoint } from "../../src/graph/graphDocument";

test("an endpoint splits at its first dot", () => {
  expect(parseEndpoint("gb1.pins.pulse")).toEqual({ node: "gb1", port: "pins.pulse" });
  expect(parseEndpoint("gb1")).toBe(null);
  expect(parseEndpoint(".x")).toBe(null);
  expect(parseEndpoint("x.")).toBe(null);
});

test("endpoint refuses a node id that would make the split ambiguous", () => {
  expect(endpoint("sys-3", "mix")).toBe("sys-3.mix");
  expect(() => endpoint("sys.3", "mix")).toThrow("contain no");
  expect(() => endpoint("", "mix")).toThrow("non-empty");
  expect(() => endpoint("sys 3", "mix")).toThrow("whitespace");  // the planner's node-id rule, the same plain-id rule
  expect(endpoint("sys-3/lsdj-sync", "serialIn")).toBe("sys-3/lsdj-sync.serialIn");
});
