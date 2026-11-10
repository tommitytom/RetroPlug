// The fixture's component: a tile view's body, drawn from the kit.
//
// It is a .tsx under the package's ts/ui/ tree, which is the one place rule 3 admits React into, and it takes the props
// the shell hands a component view (ViewComponentProps: the host, the system and the space). What it draws is
// deliberately small: a Box (the kit's chrome-reset container) holding one Text, so a typecheck proves the kit's own
// exports and JSX together, and a harness can see it if a product ever ships it.
import type { ReactNode } from "react";

import { Box, TextEx, type ViewComponentProps } from "@retroplug/ui-react";

export function Badge({ width }: ViewComponentProps): ReactNode {
  return (
    <Box style={{ width, height: 24, "background-color": "#202020" }}>
      <TextEx style={{ "text-color": "#8bc34a", "font-size": 12 }}>ui-fixture</TextEx>
    </Box>
  );
}
