// The first module of every bundle the native runner builds in its pack mode (scripts/run-native-tests.mjs --packs,
// docs/downloadable-packs-m5b-plan.md): the launch packs' app and consoles halves commit into this bundle's registries
// before any of the test's own modules evaluate (a top-level await: the modules imported after this one wait for it),
// and a run whose Mesen or BlipToaster pack did not load fails here, naming why, rather than in whichever case first
// needs a NES.
import { loadAppPacks, packStatus } from "../src/packs/loadPacks";

await loadAppPacks();
const status = packStatus();
for (const id of ["mesen", "bliptoaster"]) {
  if (!status.loaded[id]) throw new Error(`the pack mode's ${id} pack did not load: ${JSON.stringify(status)}`);
}
