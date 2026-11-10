// A UI file naming a contract module its OWN list does not carry: @retroplug/core/path is one of SHELL_IMPORTS' (the
// shell's src/ and cli/ may reach it), and the UI's list is a list of its own, so importing it here is refused.
import { joinPath } from "@retroplug/core/path";

export const joined = joinPath("a", "b");
