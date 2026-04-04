#pragma once

#include <chrono>

#include "foundation/DataBuffer.h"
#include "core/SystemSettings.h"
#include "core/RetroPlugConfig.h"

namespace rp {
	struct PingEvent {
		std::chrono::high_resolution_clock::time_point time;
	};

	struct PongEvent {
		std::chrono::high_resolution_clock::time_point time;
	};

	/*struct FetchStateRequest {};

	struct FetchSaveStateRequest {
		SystemId systemId = INVALID_SYSTEM_ID;
	};

	struct FetchSaveStateResponse {
		SystemId systemId = INVALID_SYSTEM_ID;
		orb::Uint8Buffer state;
	};

	struct LoadEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		LoadConfig config;
	};

	struct LoadSramEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		orb::Uint8Buffer sramBuffer;
	};

	struct LoadRomEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		orb::Uint8Buffer romBuffer;
	};

	struct LoadStateEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		orb::Uint8Buffer stateBuffer;
	};

	

	struct SetSettingsEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		SystemSettings settings;
	};

	struct SetDescEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		SystemDesc desc;
	};*/
}
