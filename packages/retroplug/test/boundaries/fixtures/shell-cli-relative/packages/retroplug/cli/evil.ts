// The CLI root reaches cli/ or src/, never ui/: the UI is another surface above the control plane, not a tree the
// dispatcher's modules may import.
import { App } from "../ui/App";

export const evil = App;
