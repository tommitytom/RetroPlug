// recent.json parse/serialize. Must stay compatible with the existing on-disk
// shape ({schemaVersion, entries:[{path,name}]}) so a user's current recent.json
// keeps working when the real backend is wired in. Tolerant reads: absent /
// garbage / newer-schema all yield an empty list rather than throwing.
import { test, expect } from "../../testing/harness";
import { parseRecent, serializeRecent, RECENT_SCHEMA } from "../../src/recentSerialization";

test("serialize: stamps the schema version and writes path+name entries", () => {
  const json = serializeRecent([
    { path: "/a", name: "Alias" },
    { path: "/b", name: "" },
  ]);
  const doc = JSON.parse(json);
  expect(doc.schemaVersion).toBe(RECENT_SCHEMA);
  expect(doc.entries).toEqual([
    { path: "/a", name: "Alias" },
    { path: "/b", name: "" },
  ]);
});

test("parse: reads a valid document back to entries", () => {
  const entries = [{ path: "/x", name: "N" }, { path: "/y", name: "" }];
  expect(parseRecent(serializeRecent(entries))).toEqual(entries);
});

test("parse: garbage / empty input yields an empty list", () => {
  expect(parseRecent("")).toEqual([]);
  expect(parseRecent("not json")).toEqual([]);
  expect(parseRecent("null")).toEqual([]);
  expect(parseRecent("[]")).toEqual([]); // array, not the expected object
});

test("parse: a file stamped newer than this build is refused (empty)", () => {
  const newer = JSON.stringify({ schemaVersion: RECENT_SCHEMA + 1, entries: [{ path: "/a", name: "" }] });
  expect(parseRecent(newer)).toEqual([]);
});

test("parse: an older-or-equal schema is accepted", () => {
  const older = JSON.stringify({ schemaVersion: RECENT_SCHEMA - 1, entries: [{ path: "/a", name: "" }] });
  expect(parseRecent(older)).toEqual([{ path: "/a", name: "" }]);
});

test("parse: skips malformed entries and caps the list", () => {
  const doc = {
    schemaVersion: RECENT_SCHEMA,
    entries: [
      { path: "/ok", name: "n" },
      { name: "no path" }, // dropped
      { path: "" }, // dropped
      { path: "/nameless" }, // name defaults to ""
    ],
  };
  expect(parseRecent(JSON.stringify(doc))).toEqual([
    { path: "/ok", name: "n" },
    { path: "/nameless", name: "" },
  ]);
  // cap
  const many = { schemaVersion: RECENT_SCHEMA, entries: Array.from({ length: 15 }, (_, i) => ({ path: `/p${i}`, name: "" })) };
  expect(parseRecent(JSON.stringify(many), 10).length).toBe(10);
});
