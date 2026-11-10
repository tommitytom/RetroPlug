// tagTestId: a ref callback that tags a widget's native uid with a stable testId (findByTestId).
//
// It reads the optional `__rp_tagTestId` global itself rather than through the shell's native hooks, so the kit can tag a
// widget without the AppStores types those hooks carry: the hook is one of the `__rp_*` seam's (packages/retroplug/ui/
// host/nativeHooks.ts declares it, which documents the seam for the harness that installs it), and a host that installs
// none leaves it inert. The shell's StableSlot imports this, so the function exists once.

/** The ref callback for a widget a test must find. Inert in production: `__rp_tagTestId` is installed by the UI test
 *  harness alone. */
export function tagTestId(testId: string) {
  return (node: { uid?: unknown } | null) => {
    if (node) {
      (globalThis as { __rp_tagTestId?: (uid: string, name: string) => void }).__rp_tagTestId?.(
        String(node.uid),
        testId,
      );
    }
  };
}
