// The embedded ROMs the product bakes into its binary (docs/modular-graph-phase-f4a-design.md section 5.2): mGB's synth
// is one. Which ids exist, what to call them and which console each runs on are the OWNING PACKAGE's declaration (its
// app module's `embeds`, paired with the manifest entry that names the file), so a load of an embedded ROM takes its
// platform from data rather than the shell assuming "an embedded ROM is a Game Boy".
//
// The generated list is src/generated/packages.ts, beside the roles and the providers: the same module concatenates
// every package's app contribution, in product order.
import type { Embed } from "@retroplug/core/definePackage";
import { EMBEDS } from "./generated/packages";

/** Every embedded ROM a package of this product declares, in product order. */
export function embeddedRoms(): readonly Embed[] {
  return EMBEDS;
}

/** The declaration of the embedded ROM `id` names, or undefined for an id nothing declares: what `loadEmbedded`
 *  refuses rather than booting a resource native has no bytes for. */
export function embeddedRomOf(id: string): Embed | undefined {
  return EMBEDS.find((embed) => embed.id === id);
}
