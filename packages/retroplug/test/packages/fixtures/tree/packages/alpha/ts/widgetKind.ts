// A fixture declaring package's control-plane half for a kind that is NOT a real pack's (an `accept` and a `check`), so
// the mechanism is held without naming any real declaring package.
const accepted: { id: string }[] = [];

export function acceptedWidgets(): readonly { id: string }[] {
  return accepted;
}

export default {
  accept: (items: readonly { id: string }[]) => {
    for (const item of items) accepted.push(item);
  },
  check: (items: readonly { id: string }[], data: Readonly<Record<string, Readonly<Record<string, unknown>>>>, ctx: { nodeTypes: readonly string[] }) => {
    for (const item of items) {
      const mode = (data[item.id] ?? {}).mode;
      if (typeof mode !== "string" || !ctx.nodeTypes.includes(mode)) {
        throw new Error(`the widgets item ${item.id} names ${JSON.stringify(mode)}, which no package declares`);
      }
    }
  },
};
