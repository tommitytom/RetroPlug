#pragma once

#include "core/ProxySystem.h"
#include "SameBoySystem.h"

extern "C" {
#include "SectionOffsetCollector.h"
}

namespace rp {
	class SameBoyProxySystem final : public ProxySystem<SameBoySystem> {
	private:
		Uint8Buffer _rom;
		Uint8Buffer _state;
		GB_section_offsets_t _stateOffsets;

	public:
		SameBoyProxySystem(SystemId id): ProxySystem<SameBoySystem>(id) {}

		void setup(SameBoySystem& system) override;

		MemoryAccessor getMemory(MemoryType type, AccessType access = AccessType::ReadWrite) override;

		void process(uint32 frameCount) override;

		void reset() override;

		void loadRom(const Uint8Buffer* romBuffer) override;

		bool saveSram(Uint8Buffer& target) override;

		bool saveState(Uint8Buffer& target) override;
	};
}
