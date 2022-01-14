#include <Windows.h>
#include <LivePP/API/LPP_API.h>
#include <assert.h>

#include "util/fs.h"

extern void initMain(int argc, char** argv);
extern bool mainLoop(void);
extern void destroyMain(void);

int main(int argc, char** argv) {
	fs::path currentDir(__FILE__);

	fs::path p = currentDir.parent_path().parent_path().parent_path() / "thirdparty" / "LivePP";
	HMODULE livePP = lpp::lppLoadAndRegister(p.wstring().c_str(), "RP");
	assert(livePP);

	lpp::lppApplySettingBool(livePP, "install_compiled_patches_multi_process", 1);
	lpp::lppEnableCallingModuleSync(livePP);
	lpp::lppInstallExceptionHandler(livePP);

	initMain(argc, argv);

	while (mainLoop()) {
		lpp::lppSyncPoint(livePP);

		if (lpp::lppWantsRestart(livePP) != 0) {
			lpp::lppRestart(livePP, lpp::LPP_RESTART_BEHAVIOUR_INSTANT_TERMINATION, 0u);
		}
	}

	destroyMain();

	lpp::lppShutdown(livePP);
	::FreeLibrary(livePP);

	return 0;
}
