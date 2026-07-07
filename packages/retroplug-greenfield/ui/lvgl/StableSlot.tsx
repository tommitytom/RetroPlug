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

import { View } from "lvgljs-ui";
import type { ReactNode } from "react";

// lvgljs-ui's View type doesn't expose a ref prop; cast to reach the native uid for the test-id tag.
const SlotView = View as any;

function tagSlot(testId: string) {
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
    <SlotView
      ref={tagSlot(testId)}
      style={{
        width,
        height,
        "background-opacity": 0,
        "border-width": 0,
        "padding-left": 0,
        "padding-right": 0,
        "padding-top": 0,
        "padding-bottom": 0,
        overflow: "hidden",
      }}
    >
      {children}
    </SlotView>
  );
}
