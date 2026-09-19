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

/** Just enough of a WHATWG ReadableStream for what we do with a child's pipe. Declared locally, like the
 *  `tjs` surface below, rather than pulling the DOM lib into this package's tsconfig for one type. */
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}

declare const tjs: {
  exePath: string;
  env: Record<string, string>;
  spawn(
    args: string[],
    options?: { env?: Record<string, string>; stdout?: "inherit" | "pipe" | "ignore"; stderr?: "inherit" | "pipe" | "ignore" },
  ): {
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
    stdout: ByteStream | null;
    stderr: ByteStream | null;
  };
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

/** Pump one piped stream into `sink` as it arrives. Chunks from stdout and stderr land in ONE array so
 *  the merged text keeps roughly the order the child wrote it - a TAP line and the emulator log that
 *  explains it stay together, which is the whole point of reading a failed file's output. */
async function drain(stream: ByteStream | null, sink: Uint8Array[]): Promise<void> {
  if (!stream) return;
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) sink.push(value);
  }
}

/** Like spawnSession, but CAPTURES the child's stdout+stderr instead of inheriting, and resolves with
 *  the merged text. Needed to run files concurrently: with inherited stdio several children write to
 *  the same terminal at once and the TAP streams shred each other.
 *
 *  Both pipes are drained CONCURRENTLY with the wait, not one after the other. A child that fills the
 *  stderr pipe buffer while we sit on `stdout.text()` would block forever otherwise - the classic pipe
 *  deadlock, and one that would only show up on a test file noisy enough to fill 64K. */
export async function spawnSessionCaptured(
  sessionPath: string,
  args: string[],
  env?: Record<string, string>,
): Promise<{ code: number; output: string }> {
  const proc = tjs.spawn([tjs.exePath, sessionPath, ...args], {
    env: env ? { ...tjs.env, ...env } : tjs.env,
    stdout: "pipe",
    stderr: "pipe",
  });

  const chunks: Uint8Array[] = [];
  const [, , status] = await Promise.all([
    drain(proc.stdout, chunks),
    drain(proc.stderr, chunks),
    proc.wait(),
  ]);

  // Concatenate before decoding: a multi-byte UTF-8 sequence split across two chunks would decode to
  // replacement characters if each chunk were decoded on its own.
  let total = 0;
  for (const c of chunks) total += c.length;
  const merged = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) { merged.set(c, at); at += c.length; }

  return {
    code: status.term_signal ? 1 : status.exit_status,
    output: new TextDecoder().decode(merged),
  };
}
