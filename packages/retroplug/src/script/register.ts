// How a script-node bundle hands its node definitions to the native script host
// (docs/modular-graph-phase-b3a-design.md section 2.5): it appends them to one global, which the host reads once
// the bundle has run, in each of its two contexts. A bundle loaded in several chunks (a test's toolkit, then its
// toy nodes) appends from each.
import type { DefinedNode } from "../graph/defineNode";

/** The global packages/native/src/host/script/ScriptHost.cpp reads: every definition, in registration order. */
export const SCRIPT_NODES_GLOBAL = "__rp_scriptNodes";

export function registerScriptNodes(nodes: readonly DefinedNode<any>[]): void {
  const g = globalThis as Record<string, unknown>;
  const list = (g[SCRIPT_NODES_GLOBAL] ??= []) as DefinedNode<any>[];
  list.push(...nodes);
}
