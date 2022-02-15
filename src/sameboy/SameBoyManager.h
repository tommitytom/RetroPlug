#pragma once

#include "core/SystemManager.h"
#include "util/StringUtil.h"
#include "SameBoySystem.h"
#include "SameBoyProxySystem.h"

namespace rp {
	class SameBoyManager final : public SystemManager<SameBoySystem, SameBoyProxySystem> {
	public:
		bool canLoadRom(std::string_view path) override {
			return StringUtil::endsWith(path, ".gb");
		}

		bool canLoadSram(std::string_view path) override {
			return StringUtil::endsWith(path, ".sav");
		}

		std::string getRomName(const Uint8Buffer& romData) override;

		void process(uint32 frameCount) override;
	};
}
