// StableSlot — a fixed-position wrapper for one item in a reordering/swapping list.
//
// lv_binding_js's insertChildBefore always appends (it ignores the beforeChild arg), so when React
// reorders children or swaps a child's type at a stable position, the new widget lands at the END of the
// LVGL child list, not where its JSX says. The workaround: give each item a wrapper View whose position
// in the parent NEVER changes (keyed by a stable id), and only ever swap the wrapper's SINGLE child —
// appendChild lands correctly when the parent holds at most one child. This standardizes the legacy
// SystemGrid `slot-${id}` pattern.
//
// The parent sets a stable React `key` on each StableSlot; pass the matching `testId` so the harness can
// findByTestId it (inert in production — the __rp_tagTestId hook only exists in the UI test harness).

import type { ReactNode } from "react";

import { Box } from "./Box";

/** A ref callback that tags a widget's native uid with a stable testId (findByTestId). Inert in
 *  production — the __rp_tagTestId hook only exists in the UI test harness. Shared by any widget a test
 *  needs to locate. */
export function tagTestId(testId: string) {
  return (node: { uid?: unknown } | null) => {
    if (node) (globalThis as { __rp_tagTestId?: (uid: string, name: string) => void }).__rp_tagTestId?.(String(node.uid), testId);
  };
}

export function StableSlot({
  testId,
  width,
  height,
  children,
}: {
  testId: string;
  width: number;
  height: number;
  children: ReactNode;
}) {
  return (
    <Box innerRef={tagTestId(testId)} style={{ width, height, "background-opacity": 0 }}>
      {children}
    </Box>
  );
}
