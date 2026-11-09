// n8's console contribution (docs/modular-graph-phase-f4a-design.md section 3): the physical cart its core's platform
// can drive. The console-side half (the platform, its opt-out field) is the console package's; a RealCart says only
// which platform's systems it receives what those systems send, and which node owns its link.
//
// It lives apart from app.ts, which the manifest's retroplug.consoles member names instead: this module imports the
// contract alone, so the generated consoles list the planner reads carries no app module and no zod.
import type { ConsolesContribution, RealCart } from "@retroplug/core/consoles";

// The NES is the one console RetroPlug drives a real cartridge for, and rp.n8.nes (@retroplug/n8's native half) is the
// node that owns its link; which system's traffic reaches it is buildGraph's rule (the first NES in slot order that
// does not opt out of driving a cart).
export const N8_REAL_CART: RealCart = { platform: "nes", cartNode: "rp.n8.nes" };

const N8_CONSOLES: ConsolesContribution = { realCarts: [N8_REAL_CART] };
export default N8_CONSOLES;
