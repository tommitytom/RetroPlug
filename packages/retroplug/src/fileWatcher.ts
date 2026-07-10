// FileWatcher: the TS reaction to the native file watchers. Native owns the watching —
// efsw over the config dir + bindings/, plus the per-ROM mtime poll — and reports the
// changed paths via backend.drainChangedPaths(); this drains them at idle and routes each:
//   - config.json        → UserConfigStore.reload() (re-read the preferences)
//   - bindings/<n>.json  → an onBindingsChanged refresh signal (the bindings store reads
//                          on demand, so it just needs the UI to re-query)
//   - a system's ROM     → SystemsStore.reloadSystem(id) when reloadOnRomChange is on
// (doc-03: "watcher = C++, policy = TS"). Unrecognized paths (e.g. recent.json) are ignored.

import type { Backend } from "./backend";
import type { UserConfigStore } from "./userConfigStore";
import type { SystemsStore } from "./systemsStore";
import { joinPath, dirname, extensionLower } from "./pathUtil";

/** What a pump() reacted to — for the caller (UI refresh) and for tests. */
export interface WatchReport {
  configReloaded: boolean;
  bindingsChanged: boolean;
  romReloaded: number[]; // the new ids of systems reloaded from a ROM change
}

export class FileWatcher {
  constructor(
    private readonly backend: Backend,
    private readonly userConfig: UserConfigStore,
    private readonly systems: SystemsStore,
    private readonly onBindingsChanged: () => void = () => {},
  ) {}

  /** Drain the changed paths reported by the native watcher and react. Returns what
   *  changed; a no-op (all false / empty) when nothing was reported. */
  pump(): WatchReport {
    const report: WatchReport = { configReloaded: false, bindingsChanged: false, romReloaded: [] };
    const changed = this.backend.drainChangedPaths();
    if (changed.length === 0) return report;

    const canon = (p: string) => this.backend.canonicalize(p);
    const configPath = canon(joinPath(this.backend.configDir(), "config.json"));
    const bindingsDir = canon(joinPath(this.backend.configDir(), "bindings"));
    const changedSet = new Set(changed.map(canon));

    for (const cp of changedSet) {
      if (cp === configPath) {
        if (this.userConfig.reload()) report.configReloaded = true;
      } else if (dirname(cp) === bindingsDir && extensionLower(cp) === ".json") {
        report.bindingsChanged = true;
      }
    }

    // ROM: reload every live system whose romPath changed and has reloadOnRomChange on.
    // Iterate a systems() snapshot; each reloadSystem swaps a distinct id in place, so
    // shared-ROM duplicates all reload.
    for (const sys of this.systems.systems()) {
      if (!sys.romPath || !sys.settings.reloadOnRomChange) continue;
      if (!changedSet.has(canon(sys.romPath))) continue;
      const newId = this.systems.reloadSystem(sys.id);
      if (newId !== null) report.romReloaded.push(newId);
    }

    if (report.bindingsChanged) this.onBindingsChanged();
    return report;
  }
}
