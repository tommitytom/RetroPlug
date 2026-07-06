// LSDJ sav authoring over the native host: JSON (an rp::lsdj::model::Sav, lenient) -> encoded
// .sav bytes. A test/tooling helper (not part of the production Backend seam) that lets a test
// author song/sync state directly and boot LSDJ into it — over the same
// globalThis[Symbol.for("plugin")].__rpcSend channel realBackend/audioDriver use.

type RpcSend = (request: unknown) => unknown;
interface Reply {
  result?: unknown;
  error?: { code: number; message: string };
}

function resolveSend(): RpcSend {
  const ns = (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as { __rpcSend?: RpcSend } | undefined;
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}

let nextId = 1;

/** Encode an LSDJ sav from a JSON string (an rp::lsdj::model::Sav; unset cells default). */
export function savFromJson(json: string): Uint8Array {
  const send = resolveSend();
  const reply = send({ jsonrpc: "2.0", id: nextId++, method: "savFromJson", params: [json] }) as Reply | null | undefined;
  if (reply == null) throw new Error("savFromJson: no reply");
  if (reply.error) throw new Error(`savFromJson: [${reply.error.code}] ${reply.error.message}`);
  return reply.result as Uint8Array;
}
