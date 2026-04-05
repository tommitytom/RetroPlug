#pragma once

#include "foundation/FileWatcher.h"

namespace rp {
	struct FileWatcherComponent {
		std::string entryType;
	};

	struct FileWatcherContext {
		#ifdef FW_PLATFORM_WEB
		std::unique_ptr<orb::FileWatcher> fileWatcher = std::make_unique<orb::DummyFileWatcher>();
		#else
		std::unique_ptr<orb::FileWatcher> fileWatcher = std::make_unique<orb::EfswFileWatcher>();
		#endif
	};
}
