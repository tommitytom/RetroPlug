#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "core/SystemSettings.h"
#include "lsdj/Sav.h"
#include "platform/Types.h"

namespace rp {
	struct SampleSettings {
		int32 dither = 1;
		int32 volume = 0xFF;
		int32 pitch = 0x7F;
		int32 filter = 0;
		int32 cutoff = 0x7F;
		int32 q = 0;
	};

	const SampleSettings EMPTY_SAMPLE_SETTINGS = SampleSettings{
		.dither = -1,
		.volume = -1,
		.pitch = -1,
		.filter = -1,
		.cutoff = -1,
		.q = -1,
	};

	struct KitSample {
		std::string name;
		std::string path;
		SampleSettings settings = EMPTY_SAMPLE_SETTINGS;
	};

	struct KitState {
		std::string name;
		std::vector<KitSample> samples;
		SampleSettings settings;
	};

	using KitIndex = size_t;

	class LsdjModel final : public Model {
	public:
		std::unordered_map<KitIndex, KitState> kits;

	public:
		LsdjModel(): Model("lsdj") {}

		void onSerialize(sol::state& s, sol::table target) final override;

		void onDeserialize(sol::state& s, sol::table source) final override;

		void updateKit(KitIndex kitIdx);

		void onBeforeLoad(LoadConfig& loadConfig) override {
			if (!loadConfig.sramBuffer || loadConfig.sramBuffer->size() == 0) {
				lsdj::Sav sav;
				loadConfig.sramBuffer = std::make_shared<Uint8Buffer>();
				sav.save(*loadConfig.sramBuffer);
			}
		}
	};
}