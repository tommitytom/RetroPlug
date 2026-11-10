// @retroplug/ui-react: the React half of the UI's contract.
//
// A package that wants React for a view, or the shell's own components, gets it here; @retroplug/ui stays plain
// TypeScript, so a pack author who does not want React never meets it. Everything in this package lives under ts/ui/,
// which is the one tree rule 3 admits React into, and the four React host modules (`react`, `react/jsx-runtime`,
// `lvgljs`, `lvgljs-ui`) are declared in its manifest's `retroplug.host`.
//
// The widgets are the shell's own, moved here unchanged: `lv` is the typed handle on the lvgljs-ui components, `Box` a
// View with the theme chrome zeroed, `PixelCanvas` the crisp scaled canvas, and the hooks the frame bus and a forced
// re-render. `DebugReadout` and `tagTestId` are what the debug overlays and the UI tests draw with.
export { Box, CHROME_RESET } from "./Box";
export { DebugReadout, hex2 } from "./DebugReadout";
export {
  CanvasEx,
  LV_IMAGE_ALIGN_CONTAIN,
  TextEx,
  ViewEx,
  type CanvasExProps,
  type CanvasHandle,
  type LvAlign,
  type LvEvent,
  type LvKeyEvent,
  type LvStyle,
  type TextChildren,
  type TextExProps,
  type ViewExProps,
} from "./lv";
export { PixelCanvas } from "./PixelCanvas";
export { reactView, type ViewComponentProps } from "./reactView";
export { tagTestId } from "./tagTestId";
export { useFrameDivider, useFrameSampled, useVersionedPoll } from "./useFramePoll";
export { useNativeEvent, type NativeEvents } from "./useNativeEvent";
export { useForceUpdate, useRerenderOn } from "./useRerender";
