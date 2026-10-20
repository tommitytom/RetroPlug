// The slice of lvgljs-ui the UI uses, declared for the type-check only: tsconfig.ui.json maps the bare
// specifier here, while every bundle (esbuild) still resolves the real package. It exists because the
// package ships TypeScript SOURCE rather than declarations, which tsc then checks under our strict settings
// (~1600 errors in code we don't own; skipLibCheck only skips .d.ts files).
//
// Keep it to what ui/ imports. The widgets are declared loosely on purpose: ui/lvgl/lv.ts gives each one the
// prop types native actually accepts, and that is where call sites should get them from.

import type { ComponentType, ReactElement, ReactNode } from "react";

export declare const View: unknown;
export declare const Canvas: unknown;
/** Plain styled text. For ref / key / hover props use TextEx (ui/lvgl/lv.ts). */
export declare const Text: ComponentType<{ style?: Record<string, unknown>; children?: ReactNode }>;

/** lv_align_t (lv_area.h). */
export declare const EAlignType: {
  readonly ALIGN_DEFAULT: 0;
  readonly ALIGN_TOP_LEFT: 1;
  readonly ALIGN_TOP_MID: 2;
  readonly ALIGN_TOP_RIGHT: 3;
  readonly ALIGN_BOTTOM_LEFT: 4;
  readonly ALIGN_BOTTOM_MID: 5;
  readonly ALIGN_BOTTOM_RIGHT: 6;
  readonly ALIGN_LEFT_MID: 7;
  readonly ALIGN_RIGHT_MID: 8;
  readonly ALIGN_CENTER: 9;
};

/** lv_key_t (lv_group.h) - what a widget's onKey receives. */
export declare const ELvKey: {
  readonly LV_KEY_UP: 17;
  readonly LV_KEY_DOWN: 18;
  readonly LV_KEY_RIGHT: 19;
  readonly LV_KEY_LEFT: 20;
  readonly LV_KEY_ESC: 27;
  readonly LV_KEY_DEL: 127;
  readonly LV_KEY_BACKSPACE: 8;
  readonly LV_KEY_ENTER: 10;
  readonly LV_KEY_NEXT: 9;
  readonly LV_KEY_PREV: 11;
  readonly LV_KEY_HOME: 2;
  readonly LV_KEY_END: 3;
};

/** The display, read live off the LVGL resolution. */
export declare const Dimensions: { readonly window?: { width: number; height: number } };

export declare const Render: {
  render(element: ReactElement): void;
  unmount?: () => void;
};
