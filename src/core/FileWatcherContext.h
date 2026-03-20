#pragma once

#include "foundation/FileWatcher.h"

namespace rp {
	struct FileWatcherComponent {
		std::string entryType;
	};

	struct FileWatcherContext {
		std::unique_ptr<orb::FileWatcher> fileWatcher = std::make_unique<orb::EfswFileWatcher>();
	};
}
