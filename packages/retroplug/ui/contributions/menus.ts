// The menus facade: importing this seeds the UI contributions (the generated module seeds the app's registries first,
// since the menus' checks read the roles and the tracker integrations), and re-exports the registry. It never reaches
// the views' seed: a component view imports React.
import "../generated/menus";

export { uiRegistry } from "./uiRegistries";
