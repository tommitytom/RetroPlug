#include "ArduinoboyServiceProvider.h"

#include "lsdj/ArduinoboyOverlay.h"
#include "lsdj/ArduinoboyService.h"
#include "util/GameboyUtil.h"

namespace rp {
	bool rp::ArduinoboyServiceProvider::match(const LoadConfig& loadConfig) {
		std::string_view romName = GameboyUtil::getRomName(*loadConfig.romBuffer);
		std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
		return shortName == "lsdj";
	}

	SystemOverlayPtr ArduinoboyServiceProvider::onCreateUi() {
		return std::make_shared<ArduinoboyOverlay>();
	}

	SystemServicePtr ArduinoboyServiceProvider::onCreateService() const {
		return std::make_shared<ArduinoboyService>();
	}
}
