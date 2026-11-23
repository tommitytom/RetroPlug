// The app facade: importing this seeds the app's registries (through the generated module, which seeds the consoles
// and the placers first, since the app's checks read them), and re-exports them. A shell module that reads an app
// registry imports it from here, never from the generated module or appRegistries.ts directly, so the seed has always
// run by the time anything reads.
import "../generated/packages";

export * from "./appRegistries";
