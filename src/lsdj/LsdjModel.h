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
	class LsdjModel : public SystemService {
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

		void setState(const entt::any& data) override {}

		const entt::any getState() const override { return entt::any{}; }

		void updateKit(KitIndex kitIdx);

		lsdj::MemoryOffsets& getMemoryOffsets() {
			return _ramOffsets;
		}

		bool getOffsetsValid() const {
			return _offsetsValid;
		}

		bool isRomValid() const {
			return _romValid;
		}

		//bool isSramDirty();
	};
}