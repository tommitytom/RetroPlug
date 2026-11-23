// The embedded ROMs the product bakes into its binary (docs/modular-graph-phase-f4a-design.md section 5.2): mGB's synth
// is one. Which ids exist, what to call them and which console each runs on are the OWNING PACKAGE's declaration (its
// app module's `embeds`, paired with the manifest entry that names the file), so a load of an embedded ROM takes its
// platform from data rather than the shell assuming "an embedded ROM is a Game Boy".
//
// The list is the live registry src/contributions/appRegistries.ts, which src/generated/packages.ts seeds beside the
// roles and the providers, in product order.
import type { Embed } from "@retroplug/core/definePackage";
import { embedsRegistry } from "./contributions/app";

/** Every embedded ROM a package of this product declares, in product order. */
export function embeddedRoms(): readonly Embed[] {
  return embedsRegistry.items();
}

/** The declaration of the embedded ROM `id` names, or undefined for an id nothing declares: what `loadEmbedded`
 *  refuses rather than booting a resource native has no bytes for. */
export function embeddedRomOf(id: string): Embed | undefined {
  return embedsRegistry.items().find((embed) => embed.id === id);
}
