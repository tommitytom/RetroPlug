#pragma once

#include <FileWatcher/FileWatcher.h>
#include "plugs/SameBoyPlug.h"
#include <iostream>

class RomUpdateListener : public FW::FileWatchListener {
private:
	SameBoyPlugPtr _plug;

public:
	RomUpdateListener(SameBoyPlugPtr plug): _plug(plug) {}
	~RomUpdateListener() {}

	void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
		/*if (fs::path(_plug->romPath()).filename().string() == filename && action != FW::Actions::Delete) {
			_plug->init(_plug->romPath(), _plug->model(), true);
			_plug->disableRendering(false);
		}*/
	}
};
