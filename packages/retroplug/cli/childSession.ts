// Running a session file in a CHILD retroplug-cli process, shared by the `test` and `run` tools.
//
// Both need this rather than importing the session into their own process, and for the same reason: the
// dispatcher has ALREADY booted a control plane for the tool itself (cli/cli.ts calls runSession /
// runLongSession before handing over), so a session file that calls bootSession() again would be the
// second control plane over one native Engine. That does not work - the two ProjectStores fight over
// system ids, and addSystem starts returning null.
//
// Spawning also gives `test` the per-file isolation it needs anyway: a fresh Engine per file, and a fresh
// config dir so runs never cross-contaminate.

declare const tjs: {
  exePath: string;
  env: Record<string, string>;
  spawn(
    args: string[],
    options?: { env?: Record<string, string> },
  ): { wait(): Promise<{ exit_status: number; term_signal: string | null }> };
};

/** Run `sessionPath` (a `.js` session) in a child retroplug-cli, inheriting stdio. Resolves to the
 *  child's exit code, treating a signal death as a failure. `env` overrides are merged over our own. */
export async function spawnSession(
  sessionPath: string,
  args: string[],
  env?: Record<string, string>,
): Promise<number> {
  const proc = tjs.spawn([tjs.exePath, sessionPath, ...args], {
    env: env ? { ...tjs.env, ...env } : tjs.env,
  });
  const status = await proc.wait();
  if (status.term_signal) return 1;
  return status.exit_status;
}
