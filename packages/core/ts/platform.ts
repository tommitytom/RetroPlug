// The two orthogonal axes a loaded system carries, as the role contract names them: what a ROM targets, and the
// emulator that runs it. They live here because ConstructSpec, RomContext and the console contract (./consoles.ts)
// name them, and a package may import neither the shell nor another package.
//
// Both are OPEN: a string, documented by the ids the console descriptors register ("gb", "nes", "gba", "sms", "gg";
// "sameboy", "mesen"), because a console is a package that contributes its descriptor, and a closed union here would
// put the list of consoles back in the contract. The descriptors are the authority: the shell validates a stored
// project's platform and core against them (src/projectConfig.ts), and the classifier, the node table and the battery
// rules are theirs (src/consoles.ts).
export type Platform = string;
export type Core = string;
