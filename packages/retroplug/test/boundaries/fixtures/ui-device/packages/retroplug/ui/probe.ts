// A view naming a device pack's client: the pack contributes a UI module and a CLI tool but no control-plane app module,
// which is exactly the pack a derivation keyed on `retroplug.app` alone never guards.
import { createLaunchpadClient } from "@retroplug/launchpad/client";

export const client = createLaunchpadClient;
