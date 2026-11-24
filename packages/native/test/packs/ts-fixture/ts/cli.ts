// The TS fixture pack's CLI half: one command.
import { defineCliTools } from "@retroplug/core/cliTool";

export default defineCliTools([{
  name: "tsfix-hello",
  summary: "Say hello from the TS fixture pack",
  help: "usage: retroplug-cli tsfix-hello",
  // Waits a 0 ms timer first, so the session outlives the loop pass that reports a throwing half beside this pack
  // (tsfix-throws): a tool that ends within one microtask drain would exit before that report was ever dispatched.
  run: async () => {
    await new Promise<void>((resolve) => (globalThis as unknown as { setTimeout(f: () => void, ms: number): void }).setTimeout(resolve, 0));
    console.log("hello from the ts fixture pack");
  },
}]);
