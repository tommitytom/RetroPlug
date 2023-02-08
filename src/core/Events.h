#pragma once

#include "foundation/DataBuffer.h"
#include "core/System.h"
#include "core/SystemSettings.h"
#include "core/ProjectState.h"

namespace rp {
	struct AddSystemEvent {
		SystemPtr system;
	};
	
	struct RemoveSystemEvent {
		SystemId systemId;
	};

	struct RemoveAllSystemsEvent {};

	struct ReplaceSystemEvent {
		SystemPtr system;
	};

	struct SwapSystemEvent {
		SystemPtr system;
	};

	struct ResetSystemEvent {
		SystemId systemId;
	};

	struct SetGameLinkEvent {
		SystemId systemId;
		bool enabled;
	};

	struct CollectSystemEvent {
		SystemPtr system;
	};

	struct FetchStateRequest {};

	struct SystemStateResponse {
		SystemType type = 0;
		SystemId id = INVALID_SYSTEM_ID;
		std::string romName;
		SystemDesc desc;
		SystemStateOffsets stateOffsets;
		fw::Uint8Buffer state;
		fw::Uint8Buffer rom;
		fw::DimensionU32 resolution;
		std::vector<std::pair<SystemServiceType, entt::any>> services;
	};

	struct FetchStateResponse {
		GlobalConfig config;
		ProjectState project;
		std::vector<SystemStateResponse> systems;
	};

	struct FetchSaveStateRequest {
		SystemId systemId = INVALID_SYSTEM_ID;
	};

	struct FetchSaveStateResponse {
		SystemId systemId = INVALID_SYSTEM_ID;
		fw::Uint8Buffer state;
	};

	struct LoadEvent {
		SystemId systemId = INVALID_SYSTEM_ID;
		LoadConfig config;
	};

	struct PingEvent {
		uint64 time;
	};

	struct PongEvent {
		uint64 time;
	};
}
