#include <filesystem>
#include <assert.h>
#include <Windows.h>
#include "LivePP/API/LPP_API_x64_CPP.h"

extern void initMain(int argc, char** argv);
extern bool mainLoop(void);
extern void destroyMain(void);

extern void reload(lpp::LppHotReloadPostpatchHookId, const wchar_t* const recompiledModulePath, const wchar_t* const* const modifiedFiles, unsigned int modifiedFilesCount, const wchar_t* const* const modifiedClassLayouts, unsigned int modifiedClassLayoutsCount);

LPP_HOTRELOAD_POSTPATCH_HOOK(reload);

int main(int argc, char** argv) {
	std::filesystem::path currentDir(__FILE__);

	std::filesystem::path p = currentDir.parent_path().parent_path().parent_path() / "thirdparty" / "LivePP";

	// create a synchronized Live++ agent
	lpp::LppSynchronizedAgent lppAgent = lpp::LppCreateSynchronizedAgent(p.wstring().c_str());
	if (!lpp::LppIsValidSynchronizedAgent(&lppAgent)) {
		return 1;
	}

	lppAgent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_NONE);

	initMain(argc, argv);

	while (mainLoop()) {
		// listen to hot-reload and hot-restart requests
		if (lppAgent.WantsReload()) {
			// Live++: client code can do whatever it wants here, e.g. synchronize across several threads, the network, etc.
			lppAgent.CompileAndReloadChanges(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
		}

		if (lppAgent.WantsRestart()) {
			// Live++: client code can do whatever it wants here, e.g. finish logging, abandon threads, etc.
			lppAgent.Restart(lpp::LPP_RESTART_BEHAVIOUR_INSTANT_TERMINATION, 0u);
		}
	}

	destroyMain();

	lpp::LppDestroySynchronizedAgent(&lppAgent);

	return 0;
}
