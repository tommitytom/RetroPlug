#include "system/mesen/MesenGlobalInit.hpp"

#include <mutex>

#include "Core/Shared/MessageManager.h"
#include "Utilities/FolderUtilities.h"

// Mesen reads/writes config files relative to a "home folder"; point it at /tmp so any incidental writes
// don't pollute the user's HOME. Matches kMesenHomeFolder in MesenGbaSystem.cpp.
void mesenGlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        FolderUtilities::SetHomeFolder("/tmp/retroplug-mesen");
        MessageManager::SetOptions(false, true);
    });
}
