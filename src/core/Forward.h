#pragma once

#include <memory>
#include "foundation/Types.h"

namespace rp {
	using Guid = uint32;
	using SystemType = Guid;
	using SystemServiceType = Guid;
	using SystemId = Guid;
	using SystemServiceId = Guid;

	class System;
	class SystemProcessor;
	class SystemProvider;
	class SystemFactory;
	class SystemManager;
	class SystemService;
	class SystemServiceProvider;
	class SystemAudioProcessor;
	class SystemOverlay;
	class ProxySystem;
	class Project;
	class Model;

	struct LoadConfig;

	using SystemPtr = std::shared_ptr<System>;
	using SystemProcessorPtr = std::shared_ptr<SystemProcessor>;
	using SystemProviderPtr = std::shared_ptr<SystemProvider>;
	using SystemFactoryPtr = std::shared_ptr<SystemFactory>;
	using SystemServicePtr = std::shared_ptr<SystemService>;
	using SystemServiceProviderPtr = std::shared_ptr<SystemServiceProvider>;
	using SystemAudioProcessorPtr = std::shared_ptr<SystemAudioProcessor>;
	using SystemOverlayPtr = std::shared_ptr<SystemOverlay>;
	using ProxySystemPtr = std::shared_ptr<ProxySystem>;
	using ModelPtr = std::shared_ptr<Model>;
	
	constexpr SystemId INVALID_SYSTEM_ID = -1;
	constexpr SystemServiceId INVALID_SYSTEM_SERVICE_ID = -1;
	constexpr SystemType INVALID_SYSTEM_TYPE = 0;
	constexpr SystemServiceType INVALID_SYSTEM_SERVICE_TYPE = 0;

	constexpr size_t MAX_SYSTEM_COUNT = 4;
	constexpr size_t MAX_IO_STREAMS = 16;
}
