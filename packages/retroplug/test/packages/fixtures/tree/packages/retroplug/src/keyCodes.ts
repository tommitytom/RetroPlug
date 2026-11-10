// The fixture shell's binding vocabulary, re-exported from the real shell's: the generated menus module imports
// ../../src/keyCodes for the key-name resolver and SDL's button names, and a check held to a copy of that table would
// only prove the copy. The driver aliases the bare specifier to the worktree's own file.
export * from "@retroplug/shell/keyCodes";
