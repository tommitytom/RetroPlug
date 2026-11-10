// A UI file naming a device pack's client rather than reaching it through the generated registry: no contract module,
// so the UI's list refuses it whatever the pack contributes.
import { createLaunchpadClient } from "@retroplug/launchpad/client";

export const client = createLaunchpadClient;
