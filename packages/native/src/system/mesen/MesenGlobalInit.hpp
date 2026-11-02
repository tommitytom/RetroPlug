#pragma once

#include <string>

// One-time, thread-safe initialization of Mesen's PROCESS-GLOBAL state (the "home folder" for its incidental
// config writes + the message-manager options). Both MesenNesSystem and MesenGbaSystem set the same values
// at construct; with the engine jobs' renders building cores on multiple pool workers at once
// (host/engine/OfflineJobs.hpp), those setters would race. The setup is the initializer of a function-local static in
// mesenHomeFolder(), which C++ runs exactly once, the other threads waiting for it to finish, whichever thread
// constructs the first Mesen core.
void mesenGlobalInit();

// The scratch folder Mesen writes under, and the root every caller that stages a file for Mesen to read
// (MesenSmsSystem::stageRom, MesenGbaSystem's BIOS install) must build its path from. Per PROCESS - see the
// comment on the definition for why a fixed path is not good enough. Calls mesenGlobalInit().
const std::string& mesenHomeFolder();
