// Smoke: the whole native pipeline end-to-end — CMake host binary → the
// Symbol.for("plugin") RPC namespace → the realBackend sync client → an esbuild bundle →
// the TAP harness → tjs.exit. If configDir round-trips the host's
// RETROPLUG_USER_CONFIG_DIR, every layer is wired. `__CONFIG_DIR__` is injected by the
// runner (esbuild define) to the same temp dir the host was given.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";

declare const __CONFIG_DIR__: string;

test("configDir returns the config dir the native host was given", () => {
  const be = createRealBackend();
  expect(be.configDir()).toBe(__CONFIG_DIR__);
});
