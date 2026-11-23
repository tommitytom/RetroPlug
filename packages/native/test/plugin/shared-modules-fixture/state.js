// fx:state: a module with state of its own, and a count of how many times its body ran in this context.
export const box = { count: 0 };
globalThis.__fxStateRuns = (globalThis.__fxStateRuns || 0) + 1;
