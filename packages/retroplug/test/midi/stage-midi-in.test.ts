// The frame stageMidiIn stages a message at, as audioDriver.ts puts it on the wire. The RPC reads a frame as rpcpp
// reads any integer, truncated to its low 32 bits, so native refuses only a frame that lands at 1024 or past: the
// driver refuses every frame that is not an integer from 0 to 1023 before it sends anything, as native refuses.
// Driven over a stand-in __rpcSend that records each request, so no native host is needed.
import { test, expect } from "../../testing/harness";
import { createAudioDriver } from "../../src/audioDriver";

type Request = { method: string; params: unknown[] };

// An audio driver over a channel that records every request, answering each with `true`.
function recording(): { sent: Request[]; audio: ReturnType<typeof createAudioDriver> } {
  const sent: Request[] = [];
  const key = Symbol.for("plugin");
  const g = globalThis as Record<symbol, unknown>;
  const previous = g[key];
  g[key] = {
    __rpcSend: (r: Request) => {
      sent.push(r);
      return { result: true };
    },
  };
  try {
    return { sent, audio: createAudioDriver() }; // the channel is resolved once, here
  } finally {
    g[key] = previous;
  }
}

const NOTE = [0x90, 60, 100];

test("a frame from 0 to 1023 is sent as given, and an omitted one as 0", () => {
  const { sent, audio } = recording();
  expect(audio.stageMidiIn(NOTE)).toBe(true);
  expect(audio.stageMidiIn(NOTE, 0)).toBe(true);
  expect(audio.stageMidiIn(new Uint8Array(NOTE), 1023)).toBe(true);
  expect(sent.map((r) => [r.method, ...r.params])).toEqual([
    ["stageMidiIn", NOTE, 0],
    ["stageMidiIn", NOTE, 0],
    ["stageMidiIn", NOTE, 1023],
  ]);
});

test("a frame that is no integer from 0 to 1023 is refused, and nothing is sent", () => {
  // Each of these the RPC would read as a frame it takes (1.5 as 1, NaN and -Infinity as 0, 2^32 + 5 as 5), or
  // refuse only by its low 32 bits.
  const { sent, audio } = recording();
  for (const frame of [1.5, -0.5, NaN, Infinity, -Infinity, 2 ** 32 + 5, -1, 1024, 1e9]) {
    expect(audio.stageMidiIn(NOTE, frame), `frame ${frame}`).toBe(false);
  }
  expect(sent).toEqual([]);
});
