#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "core/SystemSettings.h"
#include "core/SystemService.h"
#include "lsdj/OffsetLookup.h"
#include "lsdj/Sav.h"
#include "lsdj/LsdjSettings.h"
#include "foundation/Types.h"

namespace rp {
	class LsdjModel final : public SystemService {
	private:
		lsdj::MemoryOffsets _ramOffsets;
		bool _offsetsValid = false;
		//LsdjRefresher _refresher;
		bool _romValid = false;
		uint64 _songHash = 0;

	public:
		std::unordered_map<KitIndex, KitState> kits;

	public:
		LsdjModel(): SystemService(0x15D115D1) {}
		~LsdjModel() {}

		void onBeforeLoad(LoadConfig& loadConfig) override;

		void onAfterLoad(System& system) override;

		//void onUpdate(f32 delta) override;

		//std::string getProjectName() override;

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