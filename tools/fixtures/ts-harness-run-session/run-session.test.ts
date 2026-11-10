// A TEST file that reaches for `runSession`, which belongs to a session file run by path: it boots the control plane
// and exits the process, so calling it here would end the run before the harness reports and read as a pass.
// tools/run-cli-ts-tests.sh holds this file to a refusal.
import { test, runSession } from "retroplug-cli";

test("a test case that calls runSession", () => {
  runSession(() => {});
});
