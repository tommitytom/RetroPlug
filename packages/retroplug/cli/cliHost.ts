// The CLI surface's host (docs/modular-graph-phase-f4b-design.md section 3.2): built per tool run from the session,
// over the backend, the audio driver and the process the tool runs in. It offers the six groups a tool has - files,
// png, kits, devices, process, songs - and nothing a tool cannot do: no dialogs (a tool takes paths), no live systems
// and no views.
import type { Call, Host } from "@retroplug/core/host";
import type { Session } from "./session";
import { keepAlive, exitProcess } from "./session";
import { makeCall } from "../src/rpcClient";
import { createMidiClient } from "../src/realBackend";
import { filesPngHost } from "../src/hostGroups";
import { batteryListing } from "../src/batteryListing";

export type CliHost = Host<"files" | "png" | "kits" | "devices" | "process" | "songs">;

/** `args` is the tool's own argument vector, exactly what its `run(host, args)` was given: the dispatcher strips the
 *  command name, so a tool reads its flags from either. A tool that builds its host without them (or a session run by
 *  path, which has none) gets an empty vector. */
export function cliHost(session: Session, args: readonly string[] = []): CliHost {
  // The RPC channel is built on first use: a tool that never reaches a native facet (most of them) must not need one to
  // exist to have a host, which is also what lets a mock-tier test build one.
  let rpc: Call | undefined;
  return {
    ...filesPngHost(session.backend),
    kits: {
      compile: (id, spec) => session.audio.compileKitFormat(id, spec),
    },
    devices: {
      rpc: (method, ...params) => (rpc ??= makeCall())(method, ...params),
      // A factory rather than one client: a bridge opens as many as its run needs.
      midi: () => createMidiClient(),
    },
    process: {
      args: () => [...args],
      keepAlive: () => keepAlive(),
      exit: (code) => exitProcess(code),
    },
    songs: { list: (bytes) => batteryListing(bytes) },
  };
}
