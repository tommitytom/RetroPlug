// The port capacities the controller's session node declares, and no other package's node reads.
//
// These are the DSP kernel node's, port for port, as B3a sized them, since each output carries what that kernel
// output carried for its system; a queue drops what does not fit, and the drop is counted per port. The node's
// inputs are @retroplug/core's: the surface's traffic is an events stream with no fixed capacity, and the two
// controls are a message each.

import type { QueueCapacity } from "@retroplug/core/descriptor";

/** The LED bytes back to the surface, and the mode messages that take and release it: the kernel's 256 messages and
 *  4096 bytes, measured at 5 messages holding 206 bytes in one block. */
export const CONTROLLER_OUT: QueueCapacity = { messages: 256, bytes: 4096 };
