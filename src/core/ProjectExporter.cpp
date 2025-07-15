#include "ProjectExporter.h"

#include <spdlog/spdlog.h>

#include "core/ProjectSerializer.h"
#include "core/zipp.h"

namespace rp {
	bool ProjectExporter::exportProject(const Settings& settings, const fw::TypeRegistry& types, const ProjectState& project, const std::vector<SystemPtr>& systems, fw::Uint8Buffer& target) {
		zipp::Writer zipWriter({ .method = zipp::CompressionMethod::Deflate });

		std::unordered_set<std::string> romNames;
		std::vector<SystemDesc> systemDescs;
		for (const SystemPtr& system : systems) {
			systemDescs.push_back(system->getDesc());
			romNames.insert(systemDescs.back().paths.romPath);
		}

		if (settings.project) {
			std::string fileData = ProjectSerializer::serialize(types, project, systemDescs);
			if (fileData.size()) {
				zipWriter.add("project.rplg.lua", fileData);
			}
		}

		if (settings.includeFiles) {
			for (size_t i = 0; i < systems.size(); ++i) {
				const SystemPtr& system = systems[i];
				std::string name = fmt::format("{}-{}", i + 1, system->getRomName());

				MemoryAccessor rom = system->getMemory(MemoryType::Rom, AccessType::Read);
				MemoryAccessor sram = system->getMemory(MemoryType::Sram, AccessType::Read);

				zipWriter.add(name + ".gb", (const char*)rom.getData(), rom.getSize());
				zipWriter.add(name + ".sav", (const char*)sram.getData(), sram.getSize());
			}
		} else {

		}

		zipWriter.close();
		std::string_view buffer = zipWriter.getBuffer();

		if (!buffer.size()) {
			spdlog::error("ProjectExporter: No data to export.");
			return false;
		}

		target.resize(buffer.size());
		target.write((const uint8*)buffer.data(), buffer.size());

		return true;
	}
}
