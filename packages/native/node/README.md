# The Node-API host

A fourth host over the same backend service graph as the plugin
([src/main.cpp](../src/main.cpp)), the CLI ([cli/main.cpp](../cli/main.cpp)) and the render worker
([src/host/render/RenderHost.cpp](../src/host/render/RenderHost.cpp)), so
[packages/retroplug/src](../../retroplug/src) runs on Node with **no source changes**.

```
pnpm test:node
```

## Why this works at all

Two properties of the existing design, neither of them added for this:

1. **The TS layer has no txiki coupling.** `packages/retroplug/src` contains zero references to
   `tjs`; the only ones in the whole TS tree are `tjs.exit` in
   [cli/session.ts](../../retroplug/cli/session.ts), which a two-line shim replaces.
2. **The DSP kernel does not run in the host runtime.** `dsp.compileScript(source)` is an RPC call;
   the *native* DSP runtime compiles it into its own embedded QuickJS. So the kernel is already
   host-independent and comes along for free.

The whole backend reaches JS through one function:

```
globalThis[Symbol.for("plugin")].__rpcSend(request) -> reply     // SYNCHRONOUS
```

## Why an addon and not rpcpp's stdio client

rpcpp already ships a TypeScript client (`createClient` + `spawnStdioTransport` + JSON/Msgpack
codecs), so spawning `retroplug-cli` and talking over a pipe looks free. It is not: that client is
Promise-based, and [realBackend.ts](../../retroplug/src/realBackend.ts) calls `__rpcSend`
**synchronously** (`openFileBrowser` is explicitly the one async method, and it bypasses RPC to stay
that way). Every store is built on that. Adopting an async transport would put `await` at hundreds of
call sites through `ProjectStore` / `SystemsStore` / the UI, which is a rewrite of the TS layer, not
a port.

A native N-API callback returns synchronously, so an in-process addon honours the contract as-is.

## Layout

| file | role |
|---|---|
| `napi/{Reader,Writer,Parser,read,write}.hpp` | the reflect-cpp bridge over `napi_value` |
| `NodeCodec.hpp` | the rpcpp codec (the twin of `QuickJSCodec.h`) |
| `NodeTransport.hpp` | the rpcpp transport (the twin of `QuickJSTransport.h`) |
| `binding.cpp` | the addon entry: composes the services and exports `rpcSend` |

`napi/` and the two headers are written to drop into rpcpp as `src/napi/` verbatim (same namespace
shape, same interface as the `qjs` twin); they live here for now so the spike does not churn two
nested submodule pointers.

Both are **simpler** than the QuickJS versions, for one reason: `napi_value`s are not individually
refcounted, they belong to the enclosing handle scope. So the Reader needs no `owned_` tracking
vector and the Writer has no steal-on-attach rule.

The codec opts into rpcpp's `NativeAstCodec` via `ast_view_t`. That is load-bearing: it lets
`RpcServer` decode method params straight into their typed tuple instead of through `rfl::Generic`,
which is what keeps an `rfl::Bytestring` crossing as a live `Uint8Array` rather than degrading to an
array of numbers. `renderAudio` returns raw interleaved f32 and `compileKit` a 16 KB bank; neither
should be serialized.

## Building

Behind an option, off by default (it needs Node's N-API headers, which a build box need not have):

```
./build.sh -DRETROPLUG_NODE_ADDON=ON
cmake --build build --target retroplug-node -j$(nproc)
```

Output: `build/node/retroplug.node`. Linux/macOS for now; Windows wants an import library from the
node distribution. `pnpm test:node` reconfigures automatically if the option is off.

## Using it

```js
const addon = require("./build/node/retroplug.node");
globalThis[Symbol.for("plugin")] = { __rpcSend: addon.rpcSend, args: process.argv.slice(2) };
globalThis.tjs = { exit: (c) => { process.exitCode = c; } };

const sdk = await import("./build/cli-sdk/retroplug-cli.mjs");
const s = sdk.bootSession();
s.project.systems.loadMgb();
s.audio.stageMidiIn([0x90, 60, 100]);
const pcm = s.audio.renderAudio(250);   // Float32Array
```

(The SDK bundle is ESM but named `.js`, and the repo root `package.json` is commonjs, so Node loads
it as CJS unless you copy it to `.mjs` first.)

## Verification

Two differential suites, both comparing against the shipping QuickJS host rather than against
hand-written expectations:

- **`parity.test.mjs`** exercises the codec in isolation. One shared operation matrix
  (`ops.mjs`) runs through both hosts' `__rpcSend`: binary in both directions, nested structs,
  vectors of structs, empty optionals, zero-length buffers, error envelopes.
- **`emu-parity.test.mjs`** drives the REAL control plane on both hosts, importing the same built
  SDK bundle, and compares **rendered PCM sample for sample** along with save-state snapshots and
  the debug facet's APU reads. Both cores are MIDI-driven first, so the comparison is over actual
  signal and not two buffers of silence.

## Known gaps

- **`NodeTransport::drain` is unwired.** Nothing in the backend surface pushes async today (the CLI
  host's sink is a no-op too), so the synchronous request/response path is all that runs. Server-push
  notifications would need it hooked to the libuv loop via `napi_threadsafe_function`.
- **Handles accumulate within one call.** Decoding a very large array holds one handle per element
  until the callback returns. Bounded by a single RPC request, so it is fine here; a streaming decode
  would want per-iteration escapable scopes.
- **Distribution is the open question.** The CLI is one static binary with no runtime deps, which is
  why `sync-cli-to-bliptoaster.sh` is a `cp`. Node means a prebuilt `.node` per platform *and* ABI,
  or building the cores from source on the consumer's machine. That is the argument for keeping this
  alongside the CLI rather than replacing it.
