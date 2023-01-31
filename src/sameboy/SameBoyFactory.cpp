#include "SameBoyFactory.h"

#include "foundation/StringUtil.h"
#include "sameboy/SameBoyProcessor.h"
#include "sameboy/SameBoySystem.h"
#include "util/GameboyUtil.h"

namespace rp {
	SystemPtr SameBoyProvider::createSystem() const {
		return std::make_shared<SameBoySystem>();
	}

	bool SameBoyProvider::canLoadRom(std::string_view path) const {
		return fw::StringUtil::endsWith(path, ".gb");
	}

	bool SameBoyProvider::canLoadSram(std::string_view path) const {
		return fw::StringUtil::endsWith(path, ".sav");
	}

	std::string SameBoyProvider::getRomName(const fw::Uint8Buffer& romData) const {
		return GameboyUtil::getRomName((const char*)romData.data());
	}

	SystemProcessorPtr SameBoyProvider::createProcessor() const {
		return std::make_unique<SameBoyProcessor>(); 
	}
}
