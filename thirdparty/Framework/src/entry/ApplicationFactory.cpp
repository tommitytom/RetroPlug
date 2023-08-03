#include "ApplicationFactory.h"

#include "foundation/MacroTools.h"

#include INCLUDE_APPLICATION(APPLICATION_HEADER)

namespace fw {
	std::unique_ptr<app::Application> ApplicationFactory::create() {
		return std::make_unique<APPLICATION_IMPL>();
	}
}
