// The package's surface: the Mesen core's settings enums and the role that carries them. The app's own registration is
// the `app` module (./app), which the generated registry imports by name; importing this barrel registers nothing.
export * from "./enums";
export { MESEN_ROLE } from "./app";
