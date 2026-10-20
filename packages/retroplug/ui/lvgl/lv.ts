// lv — typed handles on the lvgljs-ui components we use, and the LVGL values the binding doesn't export.
//
// lvgljs-ui's registerComponent is declared as returning `ComponentType<Props> | string` (it returns the tag
// name), which JSX can't render without a cast, and its prop types leave out what we actually pass (`ref`,
// a Canvas's buffer handle) and misspell the event method (`stopPropogation`; native defines
// `stopPropagation`). So every call site used to cast its component `as any`. The casts live here instead,
// once, behind prop types that describe what native really accepts - so a misspelt prop on a row or a canvas
// is a type error again.
//
// Alignment needs nothing here: lvgljs-ui exports EAlignType (EAlignType.ALIGN_CENTER, ...).

import { View, Text, Canvas } from "lvgljs-ui";
import type { ComponentType, ReactNode, Ref } from "react";

export type LvStyle = Record<string, unknown>;

/** What native hands an event handler (click / key / focus): every event can be stopped from bubbling. */
export interface LvEvent {
  stopPropagation?: () => void;
}

export interface LvKeyEvent extends LvEvent {
  key: number;
}

/** lv_image_inner_align (lv_image.h). Not exported by the binding, whose own Canvas docs number it one too
 *  high. CONTAIN fits the frame inside the widget keeping aspect (letterboxing); COVER (14) fills and crops,
 *  which is what cropped the sides / top and bottom when this was 14. */
export const LV_IMAGE_ALIGN_CONTAIN = 13;

/** Props shared by every widget we render. `align` positions a child against its parent (layering overlays
 *  on a tile); lv_binding_js's `align` handler throws on undefined, so leave the key OFF rather than unset. */
interface WidgetProps {
  style?: LvStyle;
  align?: { type: number; pos: [number, number] };
  onClick?: (event: LvEvent) => void;
  onKey?: (event: LvKeyEvent) => void;
  /** Style merged in while the pointer is over the widget (LV_STATE_HOVERED). */
  onHoveredStyle?: LvStyle;
  onFocusedStyle?: LvStyle;
}

export interface ViewExProps extends WidgetProps {
  ref?: Ref<unknown>;
  children?: ReactNode;
}

export interface TextExProps extends WidgetProps {
  ref?: Ref<unknown>;
  children?: string | number | (string | number)[];
}

/** The native canvas object a Canvas ref resolves to. setBuffer copies the whole buffer and invalidates. */
export interface CanvasHandle {
  setBuffer(buffer: ArrayBuffer, width: number, height: number): void;
}

export interface CanvasExProps extends WidgetProps {
  ref?: Ref<CanvasHandle | null>;
  /** Nearest-neighbour scaling (crisp pixels). */
  nearestNeighbor?: boolean;
  /** An lv_image_inner_align value; see LV_IMAGE_ALIGN_CONTAIN. */
  innerAlign?: number;
}

export const ViewEx = View as unknown as ComponentType<ViewExProps>;
export const TextEx = Text as unknown as ComponentType<TextExProps>;
export const CanvasEx = Canvas as unknown as ComponentType<CanvasExProps>;
