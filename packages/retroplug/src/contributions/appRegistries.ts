// The app's live registries (docs/downloadable-packs-m2a-plan.md): each package's app contribution (its definePackage
// default export) fills eight of them, and a package's manifest `retroplug.embed` fills a ninth (the embedded ROMs whose
// bytes the binary carries). src/generated/packages.ts seeds them with the product's packages, in product order, and a
// pack appends (appAdditions maps one contribution onto the eight). The checks below are the ones the generator used to
// emit into that module, word for word, and they run at every commit over every registry seeded in the bundle, so a
// pack is held to exactly what a built-in package is. src/contributions/app.ts is the module the shell reads them
// through.
import type {} from "@retroplug/tracker/packageContributions";
import type {} from "@retroplug/dsp/packageContributions";
import type { RoleType, RomProvider } from "@retroplug/core/systemRoles";
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import type { KitFormat } from "@retroplug/dsp/kitContract";
import type { Embed, PackageContributions, ProjectSection } from "@retroplug/core/definePackage";
import type { ProjectServiceFactory } from "@retroplug/tracker/projectService";
import { Registry, derived, type Addition, type Contribution } from "./registry";
import { consolesRegistry } from "./consoleRegistries";
import { placersRegistry } from "./placerRegistries";

/** The settings keys a package may not contribute a section under: the shell's own (projectConfig.ts's
 *  CORE_SETTING_KEYS, which test/project/sections.test.ts holds this list to). */
export const SHELL_SECTION_KEYS: readonly string[] = ["layout", "midiRouting", "audioRouting", "zoom"];

/** The config.json root keys a package may not contribute a section under: the shell's own (userConfig.ts's
 *  CORE_CONFIG_KEYS, which test/config/config-sections.test.ts holds this list to). A copy, because userConfig.ts reads
 *  the registries and this module may not import it back. */
export const SHELL_CONFIG_KEYS: readonly string[] = ["schemaVersion", "activeKeyboardBindings", "activeGamepadBindings",
  "defaultZoom", "sramAutoSave", "render", "useNativeFileDialogs"];

/** The app's roles: the role contract's objects, which a RoleRegistry holds by kind. A console's core is the kind of its
 *  core-config role (the role is named after its core), so a descriptor naming a core nothing contributes could be
 *  classified and never configured: that check is here rather than with the consoles because the roles are these. */
export const rolesRegistry = new Registry<RoleType>("roles", (entries, view) => {
  const kinds = new Set<string>();
  for (const { item: role } of entries) {
    if (kinds.has(role.kind)) {
      throw new Error(`the app's roles declare the role kind ${role.kind} twice`);
    }
    kinds.add(role.kind);
  }
  for (const { item: descriptor } of view(consolesRegistry)) {
    if (!entries.some(({ item: role }) => role.kind === descriptor.core && role.category === "system")) {
      throw new Error(`the console ${descriptor.id} names the core ${descriptor.core}, whose system ` +
        "role no package contributes");
    }
  }
});

/** The ROM providers: the first to match a ROM fixes its feature roles' order, so this list's order is behaviour. */
export const romProvidersRegistry = new Registry<RomProvider>("romProviders");

/** The tracker integrations. A system's tracker is the first of its marker roles the system's own order places
 *  (resolveTracker), which a load orders by the ROM's marker; a marker claimed twice is refused. */
export const trackerIntegrationsRegistry = new Registry<TrackerIntegration>("trackerIntegrations", (entries) => {
  const markers = new Set<string>();
  for (const { item: integration } of entries) {
    if (markers.has(integration.markerRole)) {
      throw new Error(`the app's tracker integrations declare the marker role ${integration.markerRole} twice`);
    }
    markers.add(integration.markerRole);
  }
});

/** The kit bank formats, by the id the audio driver dispatches with. */
export const kitFormatsRegistry = new Registry<KitFormat<any>>("kitFormats", (entries) => {
  const ids = new Set<string>();
  for (const { item: format } of entries) {
    if (ids.has(format.id)) {
      throw new Error(`the app's kit formats declare the id ${format.id} twice`);
    }
    ids.add(format.id);
  }
});

/** The project sections: a block under a .rplg's `settings` which its package parses, defaults and migrates
 *  (docs/modular-graph-phase-f5a-design.md section 6.1). A key the shell owns, and a key two packages declare, are
 *  refused: the shell keeps every non-core key verbatim, so a duplicate would have one section silently replace the
 *  other's bytes in every project. */
export const projectSectionsRegistry = new Registry<ProjectSection>("projectSections", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item: section } of entries) {
    if (SHELL_SECTION_KEYS.includes(section.key)) {
      throw new Error(`the package ${pkg} declares the project section ${section.key}, which is one of the ` +
        `shell's own settings (${SHELL_SECTION_KEYS.join(", ")})`);
    }
    const owner = owners.get(section.key);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the project section ${section.key} twice`
        : `two packages declare the project section ${section.key}: ${owner} and ${pkg}`);
    }
    owners.set(section.key, pkg);
  }
});

/** The project services: a feature's control-plane half, which the shell builds one instance of per project store
 *  (docs/modular-graph-phase-f5a-design.md section 6.2). A key is a model's and a section's, so two services under one
 *  key would fight over the graph's placement; and a placer's key is the section whose model it places, so the same two
 *  rules hold where the placement is made (checked here, where the sections are known). */
export const projectServicesRegistry = new Registry<ProjectServiceFactory>("projectServices", (entries, view) => {
  const sectionOwners = new Map<string, string>();
  const sectionKeysByPkg = new Map<string, string[]>();
  for (const { pkg, item: section } of view(projectSectionsRegistry)) {
    sectionOwners.set(section.key, pkg);
    sectionKeysByPkg.set(pkg, [...(sectionKeysByPkg.get(pkg) ?? []), section.key]);
  }
  const serviceKeys = new Map<string, string>();
  for (const { pkg, item: service } of entries) {
    const owner = serviceKeys.get(service.key);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the project service ${service.key} twice`
        : `two packages declare the project service ${service.key}: ${owner} and ${pkg}`);
    }
    serviceKeys.set(service.key, pkg);
  }
  for (const { pkg, item: service } of entries) {
    // A service's key is the section it reads and the model its placer places: a key no package declares a section
    // for would read undefined where the store asserts, and one of the shell's keys would fight the shell's field.
    if (SHELL_SECTION_KEYS.includes(service.key)) {
      throw new Error(`the package ${pkg} declares the project service ${service.key}, which is one of the shell's ` +
        `own settings (${SHELL_SECTION_KEYS.join(", ")})`);
    }
    // A package that declares no section at all is not held to one: a fixture tree may replace an app module, and a
    // service key is judged against the sections the product actually declares.
    if (!sectionOwners.has(service.key) && (sectionKeysByPkg.get(pkg) ?? []).length > 0) {
      throw new Error(`the package ${pkg} declares the project service ${service.key}, which matches no project ` +
        "section (the service key is its section's key)");
    }
  }
  for (const { pkg, item: placer } of view(placersRegistry)) {
    if (SHELL_SECTION_KEYS.includes(placer.key)) {
      throw new Error(`the package ${pkg} places the graph's model ${placer.key}, which is one of the shell's own ` +
        `settings (${SHELL_SECTION_KEYS.join(", ")})`);
    }
    if (!sectionOwners.has(placer.key) && (sectionKeysByPkg.get(pkg) ?? []).length > 0) {
      throw new Error(`the package ${pkg} places the graph's model ${placer.key}, which matches no project ` +
        "section (a placer's key is its section's key)");
    }
  }
});

/** The config.json sections: a root key of the user config which its package parses, defaults and migrates
 *  (docs/modular-graph-phase-f5b-design.md section 6.1). The shell keeps every non-core root key verbatim, so the same
 *  two refusals hold: a key the shell owns, and a key two packages declare. */
export const configSectionsRegistry = new Registry<ProjectSection>("configSections", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item: section } of entries) {
    if (SHELL_CONFIG_KEYS.includes(section.key)) {
      throw new Error(`the package ${pkg} declares the config section ${section.key}, which is one of the ` +
        `shell's own keys (${SHELL_CONFIG_KEYS.join(", ")})`);
    }
    const owner = owners.get(section.key);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the config section ${section.key} twice`
        : `two packages declare the config section ${section.key}: ${owner} and ${pkg}`);
    }
    owners.set(section.key, pkg);
  }
});

/** The embedded ROMs the packages declare: the id native's resource table knows one by, the label a menu offers it
 *  under, and the console it runs on. A declaration and a manifest entry (bakedEmbedsRegistry) are one embedded ROM's
 *  two halves: the shell loads it by the declaration, native boots the bytes the manifest names, so neither can stand
 *  alone. The id belongs to the package whose bytes carry it, an id is one ROM, and its platform is a console this
 *  product runs - the same rule the consoles' own checks hold the descriptors to. */
export const embedsRegistry = new Registry<Embed>("embeds", (entries, view) => {
  const baked = view(bakedEmbedsRegistry);
  const platforms = new Set<string>();
  for (const { item: descriptor } of view(consolesRegistry)) {
    for (const platform of descriptor.platforms) platforms.add(platform.id);
  }
  for (const { pkg, item: embed } of entries) {
    const declarers = entries.filter((entry) => entry.item.id === embed.id).map((entry) => entry.pkg);
    if (declarers.length > 1) {
      const [first, second] = declarers;
      throw new Error(first === second
        ? `the package ${first} declares the embedded ROM ${embed.id} twice: an id is one ROM`
        : `two packages declare the embedded ROM ${embed.id}: ${first} and ${second}`);
    }
    const bakers = baked.filter((entry) => entry.item.id === embed.id);
    if (!bakers.length) {
      throw new Error(`the package ${pkg} declares the embedded ROM ${embed.id}, which its manifest does ` +
        "not bake in (its retroplug.embed)");
    }
    if (!bakers.some((entry) => entry.pkg === pkg)) {
      throw new Error(`the embedded ROM ${embed.id} is declared by ${pkg} and baked in by ${bakers[0].pkg}: ` +
        "the two halves belong to one package");
    }
    if (!platforms.has(embed.platform)) {
      throw new Error(`the embedded ROM ${embed.id} names the platform ${embed.platform}, which no console ` +
        "descriptor registers");
    }
  }
});

/** The manifest half of each embedded ROM: the ids a package's `retroplug.embed` puts in the binary. */
export const bakedEmbedsRegistry = new Registry<{ readonly id: string }>("bakedEmbeds", (entries, view) => {
  const declared = view(embedsRegistry);
  for (const { pkg, item } of entries) {
    if (!declared.some((entry) => entry.item.id === item.id)) {
      throw new Error(`the package ${pkg} bakes in the embedded ROM ${item.id}, which no app module ` +
        "declares (an embeds entry with its label and platform)");
    }
  }
});

/** One package's app contribution as the additions a commit takes: the eight registries an app module fills, in one
 *  order, an absent member an empty addition. The generated registry calls this once per package; a pack's loader once
 *  per pack. */
export function appAdditions(pkg: string, app: PackageContributions): Addition<any>[] {
  return [
    { registry: rolesRegistry, pkg, items: app.roles ?? [] },
    { registry: romProvidersRegistry, pkg, items: app.romProviders ?? [] },
    { registry: trackerIntegrationsRegistry, pkg, items: app.trackerIntegrations ?? [] },
    { registry: kitFormatsRegistry, pkg, items: app.kitFormats ?? [] },
    { registry: projectSectionsRegistry, pkg, items: app.projectSections ?? [] },
    { registry: projectServicesRegistry, pkg, items: app.projectServices ?? [] },
    { registry: configSectionsRegistry, pkg, items: app.configSections ?? [] },
    { registry: embedsRegistry, pkg, items: app.embeds ?? [] },
  ];
}

/** A section entry as the shell's section code reads one (projectConfig.ts's SectionEntry, configSections.ts's
 *  ConfigSectionEntry): its contributor and the package a warning names. */
type SectionEntryShape = { readonly pkg: string; readonly section: ProjectSection };

function sectionEntries(entries: readonly Contribution<ProjectSection>[]): readonly SectionEntryShape[] {
  return entries.map(({ pkg, item }) => ({ pkg, section: item }));
}

/** The project sections in the shape the section code reads: the same array until the next commit. */
export const projectSectionEntries = derived(() => sectionEntries(projectSectionsRegistry.entries()));

/** The config sections in the shape the section code reads: the same array until the next commit. */
export const configSectionEntries = derived(() => sectionEntries(configSectionsRegistry.entries()));

/** The project services with their packages, in the shape a store builds them from. */
export const projectServiceEntries = derived((): readonly { pkg: string; service: ProjectServiceFactory }[] =>
  projectServicesRegistry.entries().map(({ pkg, item }) => ({ pkg, service: item })),
);
