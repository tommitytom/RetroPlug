#pragma once

#include "SystemProcessor.h"

namespace rp {
	class SystemOrchestrator {
	private:
		SystemProcessor* _processor;
		SystemId _nextId = 1;
		OrchestratorMessageBus* _messageBus;

	public:
		SystemOrchestrator(SystemProcessor* processor, OrchestratorMessageBus* messageBus): _processor(processor), _messageBus(messageBus) {}

		SystemPtr createUiSystem(SystemType systemType, std::string_view romPath, std::string_view sramPath);

		SystemPtr createAudioSystem(SystemType systemType, const Uint8Buffer* romData, const Uint8Buffer* sramData = nullptr, const Uint8Buffer* stateData = nullptr);

		SystemPtr createAudioSystem(SystemType systemType, std::string_view romPath, std::string_view sramPath);

		void processIncoming();

		void resetSystem(SystemPtr system);

		void removeSystem(SystemId systemId);

		void removeSystem(SystemPtr system);

		void removeAllSystems();

		void duplicateSystem(SystemId systemId);

		void clear() {
			_processor->removeAllSystems();
		}

		SystemProcessor& getProcessor() {
			return *_processor;
		}

		const SystemProcessor& getProcessor() const {
			return *_processor;
		}

		std::vector<SystemPtr>& getSystems() {
			return _processor->getSystems();
		}
	};
}