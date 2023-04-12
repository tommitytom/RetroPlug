#pragma once

#include "audio/MidiMessage.h"
#include "core/Forward.h"
#include "core/System.h"
#include "core/SystemService.h"
#include "core/SystemServiceProvider.h"
#include "util/GameboyUtil.h"

namespace rp {
	const SystemServiceType MGB_SERVICE_TYPE = 0x80B80B80;

	class MgbService final : public SystemService {
	public:
		MgbService() : SystemService(MGB_SERVICE_TYPE) {}
		~MgbService() = default;

		void onMidi(System& system, const fw::MidiMessage& message) override {
			auto& queue = system.getIo()->input.serial;
			queue.tryPush(TimedByte{ .byte = message.status, .audioFrameOffset = message.offset });
			queue.tryPush(TimedByte{ .byte = message.data1, .audioFrameOffset = message.offset });
			queue.tryPush(TimedByte{ .byte = message.data2, .audioFrameOffset = message.offset });
		}
	};

	class MgbServiceProvider final : public SystemServiceProvider {
	public:
		bool match(const LoadConfig& loadConfig) override {
			std::string_view romName = GameboyUtil::getRomName(*loadConfig.romBuffer);
			return romName == "MGB";
		}

		SystemServiceType getType() override { return MGB_SERVICE_TYPE; }

		SystemOverlayPtr onCreateUi() override { return nullptr; }

		SystemServicePtr onCreateService() const override {
			return std::make_shared<MgbService>();
		}
	};
}
