// The views facade: importing this seeds the views (every package's `retroplug.views` module, which a component view's
// React reaches), and re-exports the registry. Only the UI bundle imports it.
import "../generated/views";

export { viewsRegistry } from "./uiRegistries";
