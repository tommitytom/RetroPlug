#include "OffsetLookup.h"

#include <vector>
#include <unordered_map>
#include <spdlog/spdlog.h>

#define XXH_INLINE_ALL
#include "foundation/xxhash.h"

#include "sameboy/OffsetCalculator.h"
#include "util/GameboyUtil.h"
#include "lsdj/LsdjUtil.h"

#include "OffsetLookupData.h"

using namespace rp;
using namespace rp::lsdj;

void populateMemoryOffsets(const OffsetDesc& desc, MemoryOffsets& offsets) {
	for (uint32 i = 0; i < 4; ++i) {
		offsets.channels[i].active = desc.active + i;
		offsets.channels[i].phrasePosition = desc.phrase + i;
		offsets.channels[i].chainPosition = desc.chain + i;
		offsets.channels[i].songPosition = desc.song + i;
	}

	offsets.cursorX = desc.cursorX;
	offsets.cursorY = desc.cursorY;
}

std::string formatTags(std::string_view tags) {
	if (tags.size() > 0) {
		return fmt::format(" - {}", tags);
	}

	return "";
}

bool OffsetLookup::findOffsets(const fw::Uint8Buffer& romData, MemoryOffsets& offsets, bool forceCalculate) {
	std::string romName = GameboyUtil::getRomName((const char*)romData.data());

	if (!forceCalculate) {
		uint32 romHash = XXH32(romData.data(), 0x4000, 0);

		spdlog::info("Finding ROM offsets.  Name: {}, Hash: {}", romName, romHash);

		auto found = VERSION_LOOKUP.find(romHash);
		if (found != VERSION_LOOKUP.end()) {
			const RomVersionDesc& romDesc = found->second;

			spdlog::info("Found offsets by hash: {}{}", romDesc.version.to_string(), formatTags(romDesc.tags));

			const OffsetDesc& desc = OFFSET_GROUPS[romDesc.offsetGroup];
			populateMemoryOffsets(desc, offsets);

			return true;
		}

		std::string version;

		if (LsdjUtil::getVersionFromName(romName, version)) {
			semver::version semVer = LsdjUtil::getSemVerFromVersion(version);

			for (const auto& item : VERSION_LOOKUP) {
				if (item.second.tags.empty() || item.second.tags == "stable") {
					if (item.second.version == semVer) {
						const RomVersionDesc& romDesc = item.second;

						spdlog::info("Found offsets by name: {}{}", romName, formatTags(romDesc.tags));

						const OffsetDesc& desc = OFFSET_GROUPS[romDesc.offsetGroup];
						populateMemoryOffsets(desc, offsets);

						return true;
					}
				}
			}
		}
	}

	OffsetCalculator::Context ctx;
	if (OffsetCalculator::calculate((const char*)romData.data(), ctx)) {
		spdlog::info("Found offsets by calculation");
		offsets = ctx.offsets;

		return true;
	}

	spdlog::warn("Failed to detect version: {}", romName);

	return false;
}
