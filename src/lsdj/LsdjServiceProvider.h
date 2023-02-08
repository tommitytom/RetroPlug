#pragma once

#include "core/System.h"
#include "core/SystemServiceProvider.h"
#include "lsdj/ArduinoboyOverlay.h"
#include "lsdj/ArduinoboyService.h"
#include "lsdj/LsdjOverlay.h"
#include "lsdj/LsdjService.h"
#include "util/GameboyUtil.h"

namespace rp {
	class LsdjServiceProvider : public SystemServiceProvider {
	public:
		bool match(const LoadConfig& loadConfig) override {
			std::string_view romName = GameboyUtil::getRomName(*loadConfig.romBuffer);
			std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
			return shortName == "lsdj";
		}

		SystemServiceType getType() override { return LSDJ_SERVICE_TYPE; }

		SystemOverlayPtr onCreateUi() override { 
			return std::make_shared<LsdjOverlay>();
		}

		SystemServicePtr onCreateService() const override {
			return std::make_shared<LsdjService>();
		}
	};

	class ArduinoboyServiceProvider : public SystemServiceProvider {
	public:
		bool match(const LoadConfig& loadConfig) override {
			std::string_view romName = GameboyUtil::getRomName(*loadConfig.romBuffer);
			std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
			return shortName == "lsdj";
		}

		SystemServiceType getType() override { return ARDUINOBOY_SERVICE_TYPE; }

		SystemOverlayPtr onCreateUi() override {
			return std::make_shared<ArduinoboyOverlay>();
		}

		SystemServicePtr onCreateService() const override {
			return std::make_shared<ArduinoboyService>();
		}
	};
}
