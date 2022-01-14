#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "platform/Types.h"
#include "core/SystemSettings.h"

namespace rp {
	struct SampleSettings {
		int32 dither = 0;
		int32 volume = 0xF0;
		int32 pitch = 0;
		int32 filter = 0;
		int32 cutoff = 0;
		int32 q = 0;
	};

	struct KitSample {
		std::string name;
		std::string path;
		SampleSettings settings;
	};

	struct KitState {
		std::string name;
		std::vector<KitSample> samples;
		SampleSettings settings;
	};

	using KitIndex = size_t;

	class LsdjModel : public Model {
	public:
		std::unordered_map<KitIndex, KitState> kits;

	public:
		LsdjModel(): Model("lsdj") {}

		void serialize(sol::state& s, sol::table target) final override;

		void deserialize(sol::state& s, sol::table source) final override;

		void updateKit(KitIndex kitIdx);
	};
}