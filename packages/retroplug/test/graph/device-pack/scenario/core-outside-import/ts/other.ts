// A core module no scenario reaches. The whole-core scan sees it, so the core's own allowlist must see it too: the
// scenario's allowlist only covers what the scenario imports.
import { canonicalJson } from "@retroplug/core/canonicalJson";

export function spell(): string {
  return canonicalJson(1);
}
