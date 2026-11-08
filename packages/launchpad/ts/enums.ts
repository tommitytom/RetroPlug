// The controller's own string-valued setting: where a controller app's row launches go.
//
// Index-ordered, its position EQUAL to the native integer the old DSP role's config carried: the tuple feeds zod
// (the shell's project schema) and indexes the menu's display-name array. It lives with the controller because the
// session node and the project schema both read it; the shell re-exports nothing of it (spec/06's package boundary).

/** Where a controller app's row launches go: into an emulated cart's MIDI inbox (which the tracker's own sync role
 *  then puts on the link port), or out of the host to real hardware (an Arduinoboy driving a real Game Boy). See
 *  docs/launchpad-plan.md 7.3. */
export const CONTROLLER_TARGET_VALUES = ["system", "midiOut"] as const;
export type ControllerTarget = (typeof CONTROLLER_TARGET_VALUES)[number];
