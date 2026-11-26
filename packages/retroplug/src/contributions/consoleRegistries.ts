// The consoles and the physical carts, as live registries (docs/downloadable-packs-m2a-plan.md): the generated
// src/generated/consoles.ts seeds them with the product's packages, and a pack appends. The descriptors say how a ROM
// is classified and which node type places a system; a real cart says which platform's systems reach it. The planner
// reads these, and its bundle must carry no role schema (test/script/bundle-protocol.test.ts), so this module imports
// the primitive and types only. src/consoles.ts is the module the callers read them through.
import type { ConsoleDescriptor, RealCart } from "@retroplug/core/consoles";
import { Registry } from "./registry";

/** The console descriptors, in product order, then packs' in load order. A platform id is looked up by the classifier,
 *  a stored project's validation and the menus, so one declared twice would leave one of the two consoles unreachable;
 *  a node type places a system, so two descriptors sharing one would make the node table depend on the order. (A
 *  descriptor's core must also be a system role, which appRegistries.ts checks: the roles are its.) */
export const consolesRegistry = new Registry<ConsoleDescriptor>("consoles", (entries) => {
  const platforms = new Map<string, string>();
  const nodeTypes = new Map<string, string>();
  for (const { item: descriptor } of entries) {
    for (const platform of descriptor.platforms) {
      const owner = platforms.get(platform.id);
      if (owner !== undefined) {
        throw new Error(`two console descriptors declare the platform ${platform.id}: ${owner} and ` + descriptor.id);
      }
      platforms.set(platform.id, descriptor.id);
    }
    const nodeOwner = nodeTypes.get(descriptor.nodeType);
    if (nodeOwner !== undefined) {
      throw new Error(`two console descriptors declare the node type ${descriptor.nodeType}: ` +
        `${nodeOwner} and ${descriptor.id}`);
    }
    nodeTypes.set(descriptor.nodeType, descriptor.id);
  }
});

/** The physical carts a project can drive. One cart per platform and one per node, because a document places at most
 *  one: the store's lookup, buildGraph's placement and the menu's jobs all resolve a platform's cart, and with two
 *  registered the answer would be whichever was registered last. A cart whose platform no console registers yet is
 *  dormant, not an error (docs/downloadable-packs-m5b-plan.md): a built-in cart beside a console a pack brings (the N8
 *  beside Mesen's NES) waits for the pack's consoles, and src/consoles.ts lists only the carts whose platform is
 *  registered, so a dormant cart is never offered or placed. */
export const realCartsRegistry = new Registry<RealCart>("realCarts", (entries) => {
  const cartPlatforms = new Map<string, string>();
  const cartNodes = new Map<string, string>();
  for (const { item: realCart } of entries) {
    const platformOwner = cartPlatforms.get(realCart.platform);
    if (platformOwner !== undefined) {
      throw new Error(`two real carts name the platform ${realCart.platform}: ${platformOwner} and ` +
        realCart.cartNode);
    }
    cartPlatforms.set(realCart.platform, realCart.cartNode);
    const nodeOwner = cartNodes.get(realCart.cartNode);
    if (nodeOwner !== undefined) {
      throw new Error(`two real carts name the cart node ${realCart.cartNode}: ${nodeOwner} and ` +
        realCart.platform);
    }
    cartNodes.set(realCart.cartNode, realCart.platform);
  }
});
