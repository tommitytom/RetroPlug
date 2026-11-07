// The package's surface: the SameBoy core's settings enums, the role that carries them and the cartridge title a ROM
// provider matches a Game Boy cart by. The app's own registration is the `app` module (./app), which the generated
// registry imports by name; importing this barrel registers nothing.
export * from "./enums";
export { SAMEBOY_ROLE } from "./app";
export { cartridgeTitle } from "./cartridge";
