// The UI's subscription to the live registries (docs/downloadable-packs-m2a-plan.md): the store channels re-render what
// the stores hold, and this re-renders what the registries hold (a pack's menu rows, actions, services and views). The
// counter moves only at a commit, which is rare (a launch, a pack), so a component reading it re-renders then and only
// then.
import { useSyncExternalStore } from "react";
import { contributionsVersion, subscribeContributions } from "../../src/contributions/registry";

/** The commit counter, re-rendering the caller when it moves: a key or a memo dependency over the registries. */
export function useContributionsVersion(): number {
  return useSyncExternalStore(subscribeContributions, contributionsVersion);
}
