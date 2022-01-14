#include "LsdjModel.h"

#include <sol/sol.hpp>

#include "lsdj/Rom.h"
#include "lsdj/KitUtil.h"

using namespace rp;

void LsdjModel::updateKit(KitIndex kitIdx) {
	lsdj::Rom rom = getSystem()->getMemory(MemoryType::Rom, AccessType::Read);
	if (!rom.isValid()) {
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
}

sol::table serializeSettings(sol::state& s, const SampleSettings& settings) {
	return s.create_table_with(
		"dither", settings.dither,
		"volume", settings.volume,
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
	s.pitch = data["pitch"].get_or(s.pitch);
	s.filter = data["filter"].get_or(s.filter);
	s.cutoff = data["cutoff"].get_or(s.cutoff);
	s.q = data["q"].get_or(s.q);

	return s;
}

void LsdjModel::serialize(sol::state& s, sol::table target) {
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

void LsdjModel::deserialize(sol::state& s, sol::table source) {
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
