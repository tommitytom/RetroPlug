#pragma once

#include <memory>
#include "foundation/Types.h"

namespace rp {
	using Guid = uint32;
	using SystemType = Guid;
	using SystemServiceType = Guid;
	using SystemId = Guid;
	using SystemServiceId = Guid;

	class SystemOverlay;
	class Project;

	struct LoadConfig;

	using SystemOverlayPtr = std::shared_ptr<SystemOverlay>;

	constexpr SystemId INVALID_SYSTEM_ID = -1;
	constexpr SystemServiceId INVALID_SYSTEM_SERVICE_ID = -1;
	constexpr SystemType INVALID_SYSTEM_TYPE = 0;
	constexpr SystemServiceType INVALID_SYSTEM_SERVICE_TYPE = 0;

	constexpr size_t MAX_SYSTEM_COUNT = 4;
	constexpr size_t MAX_IO_STREAMS = 16;
}
