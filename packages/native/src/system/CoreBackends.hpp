#pragma once

class SystemFactory;

// Register the built-in core backends (sameboy + mesen) on `factory`: the Engine's one factory, which it registers
// them on at construction. Dispatch is by the typed config a build request carries, not by a name: Mesen's config
// alternative picks the console it builds (NES, GBA, or Master System / Game Gear).
void registerCoreBackends(SystemFactory& factory);
