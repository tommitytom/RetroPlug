#pragma once

#include "core/SystemServiceProvider.h"
#include "core/System.h"
#include "util/GameboyUtil.h"
#include "lsdj/LsdjService.h"

namespace rp {
	class LsdjServiceProvider : public SystemServiceProvider {
	public:
		bool match(const LoadConfig& loadConfig) override {
			std::string_view romName = GameboyUtil::getRomName(*loadConfig.romBuffer);
			std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
			return shortName == "lsdj";
		}

		SystemServiceType getType() override { return 0x15D115D1; }

		fw::ViewPtr onCreateUi() override { return nullptr; }

		SystemServicePtr onCreateService() const override {
			return std::make_shared<LsdjService>();
		}
	};
}
