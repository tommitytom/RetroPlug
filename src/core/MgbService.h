#pragma once

#include "audio/MidiMessage.h"
#include "core/Forward.h"
#include "core/System.h"
#include "core/SystemService.h"
#include "core/SystemServiceProvider.h"
#include "util/GameboyUtil.h"

namespace rp {
	class MgbService final : public SystemService {
	public:
		MgbService() : SystemService(0x80B80B80) {}
		~MgbService() = default;

		void onMidi(System& system, const fw::MidiMessage& message) override {
			auto& queue = system.getIo()->input.serial;
			queue.tryPush(TimedByte{ .audioFrameOffset = message.offset, .byte = message.status });
			queue.tryPush(TimedByte{ .audioFrameOffset = message.offset, .byte = message.data1 });
			queue.tryPush(TimedByte{ .audioFrameOffset = message.offset, .byte = message.data2 });
		}

		void setState(const entt::any& data) override {}

		const entt::any getState() const override { return entt::any{}; }
	};

	class MgbServiceProvider : public SystemServiceProvider {
	public:
		bool match(const LoadConfig& loadConfig) override {
			std::string_view romName = GameboyUtil::getRomName(*loadConfig.romBuffer);
			return romName == "MGB";
		}

		SystemServiceType getType() override { return 0x80B80B80; }

		fw::ViewPtr onCreateUi() override { return nullptr; }

		SystemServicePtr onCreateService() const override {
			return std::make_shared<MgbService>();
		}
	};
}
