// A component in the package's UI tree. Its JSX imports react/jsx-runtime implicitly, and its `react` type import is
// the other half of what a .tsx needs; this fixture's point is that the manifest declares one of the two, not both.
import type { ReactNode } from "react";
import { View } from "lvgljs-ui";

export function Badge(): ReactNode {
  return <View />;
}
