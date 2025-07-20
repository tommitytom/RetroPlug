#include "LsdjServiceProvider.h"

#include "core/System.h"
#include "lsdj/LsdjOverlay.h"
#include "lsdj/LsdjService.h"
#include "util/GameboyUtil.h"

namespace rp {
	bool LsdjServiceProvider::match(const LoadConfig& loadConfig) {
		std::string_view romName = GameboyUtil::getRomName(*loadConfig.romBuffer);
		std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
		return shortName == "lsdj";
	}

	std::string LsdjServiceProvider::getProjectName(System& system) const {
		std::string savPath = system.getDesc().paths.sramPath;
		if (savPath.size()) savPath = fw::FsUtil::getFilename(savPath);

		const MemoryAccessor sram = system.getMemory(MemoryType::Sram, AccessType::Read);
		if (sram.isValid()) {
			const lsdj::Sav sav(sram.getBuffer());
			lsdj::Project project(sav.getWorkingProject());

			if (project.isValid()) {
				std::string name = std::string(project.getName());

				if (savPath.size()) {
					name += " (" + savPath + ")";
				}

				return name;
			}
		}

		return savPath;
	}

	SystemOverlayPtr LsdjServiceProvider::onCreateUi() {
		return std::make_shared<LsdjOverlay>();
	}

	SystemServicePtr LsdjServiceProvider::onCreateService() const {
		return std::make_shared<LsdjService>();
	}
}
