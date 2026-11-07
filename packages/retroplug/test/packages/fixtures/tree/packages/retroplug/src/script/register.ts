// The fixture shell's script host: the generated entry registers through it, as the real shell's script/register hands
// its nodes to the script runtimes. This one records the ids where the generator's driver can read them.
export function registerScriptNodes(nodes: readonly { id: string }[]): void {
  (globalThis as any).__rp_registered = nodes.map((node) => node.id);
}
