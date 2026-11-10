// The frame bus, so every declared host module is used: lvgljs here, lvgljs-ui and react through the component, and
// react/jsx-runtime through its JSX.
import { on } from "lvgljs";

export { Badge } from "./Badge";

export const listen = on;
