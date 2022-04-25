#include "LsdjModel.h"

#include <sol/sol.hpp>

#include "lsdj/KitUtil.h"
#include "lsdj/Rom.h"
#include "util/HashUtil.h"
#include "util/StringUtil.h"

using namespace rp;

sol::table serializeSettings(sol::state& s, const SampleSettings& settings) {
	return s.create_table_with(
		"dither", settings.dither,
		"volume", settings.volume,
		"gain", settings.gain,
		"pitch", settings.pitch,
		"filter", settings.filter,
		"cutoff", settings.cutoff,
		"q", settings.q
	);
}

SampleSettings deserializeSettings(sol::table data) {
	SampleSettings s;

	s.dither = data["dither"].get_or(s.dither);
	s.volume = data["volume"].get_or(s.volume);
	s.gain = data["gain"].get_or(s.gain);
	s.pitch = data["pitch"].get_or(s.pitch);
	s.filter = data["filter"].get_or(s.filter);
	s.cutoff = data["cutoff"].get_or(s.cutoff);
	s.q = data["q"].get_or(s.q);

	return s;
}

void LsdjModel::updateKit(KitIndex kitIdx) {
	lsdj::Rom rom = getSystem()->getMemory(MemoryType::Rom, AccessType::Write);
	if (!rom.isValid()) {
		spdlog::error("Failed to write kit: Rom data is not available to write");
		return;
	}

	auto found = kits.find(kitIdx);
	if (found == kits.end()) {
		spdlog::error("Failed to update sample buffers - kit not found");
		return;
	}

	std::vector<KitUtil::SampleData> sampleBuffers;
	for (const KitSample& sample : found->second.samples) {
		sampleBuffers.push_back(KitUtil::loadSample(sample.path));
	}

	lsdj::Kit kit = rom.getKit(kitIdx);
	KitUtil::patchKit(kit, found->second, sampleBuffers);

	setRequiresSave(true);
}

KitIndex LsdjModel::addKit(SystemPtr system, const std::string& path, KitIndex kitIdx) {
	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Write);
	if (!rom.isValid()) {
		return -1;
	}

	bool newKit = kitIdx == -1;
	if (kitIdx == -1) {
		kitIdx = rom.nextEmptyKitIdx();
	} else {
		kits.erase(kitIdx);
		newKit = rom.kitIsEmpty(kitIdx);
	}

	std::vector<std::byte> fileData = fsutil::readFile(path);
	rom.getKit(kitIdx).setKitData(Uint8Buffer((uint8*)fileData.data(), fileData.size()));

	setRequiresSave(true);

	return kitIdx;
}

KitIndex LsdjModel::addKitSamples(SystemPtr system, const std::vector<std::string>& paths, std::string_view name, KitIndex kitIdx) {
	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
	if (!rom.isValid()) {
		return -1;
	}

	bool newKit = kitIdx == -1;
	if (kitIdx == -1) {
		kitIdx = rom.nextEmptyKitIdx();
	} else {
		newKit = rom.kitIsEmpty(kitIdx);
	}

	if (kitIdx != -1) {
		std::string kitName = name.empty() ? fmt::format("KIT {0:x}", kitIdx) : std::string(name);
		kitName = kitName.substr(0, std::min(lsdj::Kit::NAME_SIZE, kitName.size()));

		KitState kitState = KitState{
			.name = StringUtil::toUpper(kitName),
		};

		for (const std::string& path : paths) {
			if (fsutil::getFileExt(path) == ".wav") {
				std::string sampleName = fsutil::getFilename(path);
				sampleName = fsutil::removeFileExt(sampleName);
				sampleName = std::string(fsutil::removeUniqueId(sampleName));

				sampleName = sampleName.substr(0, std::min(lsdj::Kit::SAMPLE_NAME_SIZE, sampleName.size()));

				kitState.samples.push_back(KitSample{
					.name = StringUtil::toUpper(sampleName),
					.path = path
				});
			}
		}

		if (kitState.samples.size() > 0) {
			auto found = kits.find(kitIdx);

			if (found != kits.end()) {
				for (const KitSample& sample : kitState.samples) {
					found->second.samples.push_back(sample);
				}
			} else {
				kits[kitIdx] = kitState;
			}
			
			updateKit(kitIdx);
			setRequiresSave(true);
		}
	}

	return kitIdx;
}

bool LsdjModel::isSramDirty() {
	MemoryAccessor buffer = getSystem()->getMemory(MemoryType::Sram, AccessType::Read);
	if (buffer.isValid()) {
		Uint8Buffer songBuffer = buffer.getBuffer().slice(0, LSDJ_SONG_BYTE_COUNT);
		uint64 songHash = HashUtil::hash(songBuffer);

		if (songHash != _songHash) {
			_songHash = songHash;
			return true;
		}
	} else {
		spdlog::warn("Failed to check SRAM: Buffer is invalid");
	}

	return false;
}

void LsdjModel::onBeforeLoad(LoadConfig& loadConfig) {
	if ((!loadConfig.sramBuffer || loadConfig.sramBuffer->size() == 0) && !loadConfig.stateBuffer) {
		lsdj::Sav sav;
		loadConfig.sramBuffer = std::make_shared<Uint8Buffer>();
		sav.save(*loadConfig.sramBuffer);
	}
}

void LsdjModel::onAfterLoad(SystemPtr system) {
	MemoryAccessor buffer = system->getMemory(MemoryType::Rom, AccessType::Read);
	lsdj::Rom rom(buffer);

	if (rom.isValid()) {
		_romValid = true;
		_offsetsValid = lsdj::OffsetLookup::findOffsets(buffer.getBuffer(), _ramOffsets, false);

		if (_offsetsValid) {
			//_refresher.setSystem(system, _ramOffsets);
		} else {
			spdlog::warn("Failed to find ROM offsets");
		}
	}
}

void LsdjModel::onUpdate(f32 delta) {

}

std::string LsdjModel::getProjectName() { 
	MemoryAccessor buffer = getSystem()->getMemory(MemoryType::Sram, AccessType::Read);
	if (!buffer.isValid()) {
		return "";
	}

	lsdj::Sav sav(buffer.getBuffer());

	lsdj::Project project = sav.getWorkingProject();
	std::string name;

	if (project.isValid()) {
		name = std::string(sav.getWorkingProject().getName());
	} else {
		name = "Untitled";
	}

	if (sav.getProjectCount() > 0) {
		name = fmt::format("{} (+{})", name, sav.getProjectCount());
	}
	
	return name;
}

void LsdjModel::onSerialize(sol::state& s, sol::table target) {
	sol::table kitTable = target.create_named("kits");

	for (auto& [kitIdx, kitState] : kits) {
		sol::table samplesTable = s.create_table();

		for (size_t i = 0; i < kitState.samples.size(); ++i) {
			const KitSample& sample = kitState.samples[i];

			samplesTable.add(s.create_table_with(
				"name", sample.name,
				"path", sample.path,
				"settings", serializeSettings(s, sample.settings)
			));
		}

		kitTable.set(kitIdx, s.create_table_with(
			"name", kitState.name,
			"samples", samplesTable,
			"settings", serializeSettings(s, kitState.settings)
		));
	}
}

void LsdjModel::onDeserialize(sol::state& s, sol::table source) {
	kits.clear();

	sol::table kitsTable = source["kits"];

	for (auto& kit : kitsTable) {
		size_t idx = kit.first.as<size_t>();
		sol::table kitTable = kit.second;
		sol::table samplesTable = kitTable["samples"];

		KitState kitState = {
			.name = kitTable["name"],
			.settings = deserializeSettings(kitTable["settings"])
		};

		for (size_t i = 1; i <= samplesTable.size(); ++i) {
			sol::table sampleTable = samplesTable[i];

			kitState.samples.push_back(KitSample {
				.name = sampleTable["name"],
				.path = sampleTable["path"],
				.settings = deserializeSettings(sampleTable["settings"])
			});
		}

		kits[idx] = std::move(kitState);

		updateKit(idx);
	}
}
