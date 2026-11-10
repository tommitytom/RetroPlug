// A UI module named beside the tree instead of inside it (ts/ui.ts, not ts/ui/index.ts). The manifest names a file, and
// a React host module is admissible only from the ts/ui/ tree, so a UI module outside it would import React with no
// rule seeing it; the placement is the one refusal here too.
export default {};
