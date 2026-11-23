// The TS fixture pack's CLI half: one command.
import { defineCliTools } from "@retroplug/core/cliTool";

export default defineCliTools([{
  name: "tsfix-hello",
  summary: "Say hello from the TS fixture pack",
  help: "usage: retroplug-cli tsfix-hello",
  run: () => { console.log("hello from the ts fixture pack"); },
}]);
