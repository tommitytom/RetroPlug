// The first module of every bundle the native runner builds in its pack mode (scripts/run-native-tests.mjs --packs,
// docs/downloadable-packs-m5b-plan.md): the launch packs' app and consoles halves commit into this bundle's registries
// before any of the test's own modules evaluate (a top-level await: the modules imported after this one wait for it),
// and a run whose Mesen pack did not load fails here, naming why, rather than in whichever case first needs a NES.
import { loadAppPacks, packStatus } from "../src/packs/loadPacks";

await loadAppPacks();
const status = packStatus();
if (!status.loaded.mesen) {
  throw new Error(`the pack mode's Mesen pack did not load: ${JSON.stringify(status)}`);
}
