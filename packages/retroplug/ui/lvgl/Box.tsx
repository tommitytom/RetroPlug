// Box — a <View> with the LVGL default-theme chrome zeroed out.
//
// lv_binding_js applies lv_theme_default (DARK) to every lv_obj it creates, which paints a rounded ~grey
// border and interior padding on a plain <View>. Any full-bleed container that doesn't explicitly reset
// those leaks a white rounded edge, and the padding pushes a fixed-size child past the viewport so the
// (also-theme-default) scrollable flag draws a scrollbar. The legacy shell fought this by repeating the
// same border/radius/padding=0 + overflow:hidden block at every call site (verbatim, ~4×). Box bakes that
// reset in once and merges the caller's style on top — so `<Box style={{ "border-width": 1 }}>` re-opts
// into a border, `overflow: "auto"` re-enables scroll, and everything else stays square, padding-free, and
// non-scrolling by default. Reach for Box instead of <View> for any container.

import { View } from "lvgljs-ui";
import type { ReactNode, Ref } from "react";

// lvgljs-ui's View type doesn't surface ref / onClick / align in its public typings; cast to reach them
// (the same trick the call sites used directly).
const ViewAny = View as any;

// The theme-chrome reset. Caller style overrides any of these via object-spread precedence.
export const CHROME_RESET = {
  "border-width": 0,
  "border-radius": 0,
  "padding-left": 0,
  "padding-right": 0,
  "padding-top": 0,
  "padding-bottom": 0,
  "row-spacing": 0,
  "column-spacing": 0,
  overflow: "hidden",
} as const;

export function Box({
  style,
  onClick,
  align,
  innerRef,
  children,
}: {
  style?: Record<string, unknown>;
  onClick?: () => void;
  align?: unknown;
  /** Forwarded to the View's native ref (findByTestId tagging, canvas handles, …). */
  innerRef?: Ref<unknown>;
  children?: ReactNode;
}) {
  // Only forward the optional props that are actually set. lv_binding_js's prop iterator runs a handler
  // for every own-key of the props object, and its `align` handler throws on undefined — so an unset
  // `align={undefined}` would crash the reconcile. Building the object conditionally keeps unset keys off.
  const props: Record<string, unknown> = { style: { ...CHROME_RESET, ...style } };
  if (innerRef !== undefined) props.ref = innerRef;
  if (onClick !== undefined) props.onClick = onClick;
  if (align !== undefined) props.align = align;
  return <ViewAny {...props}>{children}</ViewAny>;
}
