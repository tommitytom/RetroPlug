// A one-bit signal that the Menu currently owns a modal input mode (a capture row armed, or a prompt
// overlay open). App's Esc handler reads it to defer — so Esc cancels the modal instead of closing the
// whole menu. Menu flips it from a useEffect (after render), so it's still set during the synchronous Esc
// dispatch that cancels the modal: App bails, Menu cancels, then the effect clears it. Module-level (one
// menu is ever modal at a time), mirroring realBackend's module-level pending-browse resolver.

let active = false;

export function isMenuModalActive(): boolean {
  return active;
}

export function setMenuModalActive(next: boolean): void {
  active = next;
}
