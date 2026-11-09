// A contributed row's separator (docs/modular-graph-phase-f4b-design.md section 7.5, item 6): `separatorBefore` is an
// id a set of rows share so the fence appears before whichever of them shows first, ONCE. The recorded menus pin the
// case where one carrier shows (the display split's palette group beside the colour group), so the emitted-once rule is
// held here with two carriers visible at the same time - a second separator would draw a gap in the middle of a block
// that has one fence, and no shipped console produces that arrangement today.
import { test, expect } from "@retroplug/testing";
import { roleRowItems } from "../../ui/screens/menu/configRows";
import type { RoleType } from "@retroplug/core/systemRoles";
import type { RoleRow } from "@retroplug/ui/contribution";

/** A role whose schema declares the two keys the rows below read, which is all a row's fallback needs. */
const roleType = (kind: string): RoleType | undefined =>
  kind === "test-role"
    ? ({
        kind,
        schema: { parse: (v: Record<string, unknown>) => ({ a: "x", b: "y", ...v }) },
      } as unknown as RoleType)
    : undefined;

const SYSTEM = { platform: "gb", roles: [{ kind: "test-role", config: {} }] };

const ROW_A: RoleRow = {
  id: "row-a",
  label: "A",
  role: "test-role",
  key: "a",
  choices: [
    { value: "x", label: "X" },
    { value: "y", label: "Y" },
  ],
  separatorBefore: "group-sep",
};
const ROW_B: RoleRow = { ...ROW_A, id: "row-b", key: "b" };

const apply = (): void => {};

test("two visible carriers of one separatorBefore id emit it once, before the first", () => {
  const items = roleRowItems([ROW_A, ROW_B], SYSTEM, roleType, apply);
  expect(items.map((i) => i.id)).toEqual(["group-sep", "row-a", "row-b"]);
  expect(items[0].kind).toBe("separator");
});

test("the separator follows the first carrier that SHOWS, not the first declared", () => {
  // The first carrier is gated to a platform this system is not, so the second one opens the fence instead.
  const gated: RoleRow = { ...ROW_A, platforms: ["nes"] };
  const items = roleRowItems([gated, ROW_B], SYSTEM, roleType, apply);
  expect(items.map((i) => i.id)).toEqual(["group-sep", "row-b"]);
});

test("with every carrier hidden, no separator is emitted", () => {
  // Both carriers are gated the same way: with neither visible there is no separator at all, and the block that owns
  // them draws nothing rather than a stray line.
  const nesOnly: RoleRow = { ...ROW_A, platforms: ["nes"] };
  const nesOnlyB: RoleRow = { ...ROW_B, platforms: ["nes"] };
  expect(roleRowItems([nesOnly, nesOnlyB], SYSTEM, roleType, apply)).toEqual([]);
});
