#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "core/SystemSettings.h"
#include "lsdj/OffsetLookup.h"
#include "lsdj/Sav.h"
#include "platform/Types.h"

namespace rp {
	struct SampleSettings {
		int32 dither = 0xFF;
		int32 volume = 0xFF;
		int32 gain = 0x1;
		int32 pitch = 0x7F;
		int32 filter = 0;
		int32 cutoff = 0x7F;
		int32 q = 0;
	};

	const SampleSettings EMPTY_SAMPLE_SETTINGS = SampleSettings{
		.dither = -1,
		.volume = -1,
		.gain = -1,
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

		SampleSettings getSampleSettings(size_t sampleIdx) const {
			SampleSettings s = samples[sampleIdx].settings;
			if (s.cutoff == -1) s.cutoff = settings.cutoff;
			if (s.dither == -1) s.dither = settings.dither;
			if (s.filter == -1) s.filter = settings.filter;
			if (s.pitch == -1) s.pitch = settings.pitch;
			if (s.q == -1) s.q = settings.q;
			if (s.volume == -1) s.volume = settings.volume;
			if (s.gain == -1) s.gain = settings.gain;

			return s;
		}
	};

	using KitIndex = size_t;

	class LsdjModel final : public Model {
	private:
		lsdj::MemoryOffsets _ramOffsets;
		bool _offsetsValid = false;
		//LsdjRefresher _refresher;
		bool _romValid = false;
		uint64 _songHash = 0;

	public:
		std::unordered_map<KitIndex, KitState> _kits;

	public:
		LsdjModel(): Model("lsdj") {}
		~LsdjModel() {}

		void onSerialize(sol::state& s, sol::table target) override;

		void onDeserialize(sol::state& s, sol::table source) override;

		void onBeforeLoad(LoadConfig& loadConfig) override;

		void onAfterLoad(SystemPtr system) override;

		void onUpdate(f32 delta) override;

		std::string getProjectName() override;

		void updateKit(KitIndex kitIdx);

		KitIndex addKit(SystemPtr system, const std::string& path, KitIndex kitIdx = -1);

		KitIndex addKitSamples(SystemPtr system, const std::vector<std::string>& paths, std::string_view name = "", KitIndex kitIdx = -1);

		lsdj::MemoryOffsets& getMemoryOffsets() {
			return _ramOffsets;
		}

		bool getOffsetsValid() const {
			return _offsetsValid;
		}

		bool isRomValid() const {
			return _romValid;
		}

		bool isSramDirty();
	};

	using LsdjModelPtr = std::shared_ptr<LsdjModel>;
}