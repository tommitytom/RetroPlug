#include "ProjectExporter.h"

#include <spdlog/spdlog.h>

#include "core/ProjectSerializer.h"
#include "foundation/zipp.h"

using namespace rp;

bool ProjectExporter::exportProject(Project& project, fw::Uint8Buffer& target) {
	zipp::Writer zipWriter({ .method = zipp::CompressionMethod::Deflate });

	std::vector<SystemPtr>& systems = project.getSystems();

	/*std::string fileData = ProjectSerializer::serialize(project.getState(), systems);
	if (fileData.size()) {
		zipWriter.add("project.rplg.lua", fileData);

		for (size_t i = 0; i < systems.size(); ++i) {
			SystemPtr system = systems[i]->getSystem();
			std::string name = fmt::format("{}-{}", i + 1, system->getRomName());
			
			MemoryAccessor rom = system->getMemory(MemoryType::Rom, AccessType::Read);
			MemoryAccessor sram = system->getMemory(MemoryType::Sram, AccessType::Read);
			
			zipWriter.add(name + ".gb", (const char*)rom.getData(), rom.getSize());
			zipWriter.add(name + ".sav", (const char*)sram.getData(), sram.getSize());
		}

		zipWriter.close();
		std::string_view buffer = zipWriter.getBuffer();

		if (buffer.size()) {
			target.resize(buffer.size());
			target.write((const uint8*)buffer.data(), buffer.size());
			return true;
		}		
	}*/

	assert(false);

	return false;
}

bool ProjectExporter::exportRomsAndSavs(Project& project, fw::Uint8Buffer& target) {
	zipp::Writer zipWriter({ .method = zipp::CompressionMethod::Deflate });

	const std::vector<SystemPtr>& systems = project.getSystems();

	for (size_t i = 0; i < systems.size(); ++i) {
		const SystemPtr system = systems[i];
		std::string name = fmt::format("{}-{}", i + 1, system->getRomName());

		const MemoryAccessor rom = system->getMemory(MemoryType::Rom, AccessType::Read);
		const MemoryAccessor sram = system->getMemory(MemoryType::Sram, AccessType::Read);

		zipWriter.add(name + ".gb", (const char*)rom.getData(), rom.getSize());
		zipWriter.add(name + ".sav", (const char*)sram.getData(), sram.getSize());
	}

	zipWriter.close();
	std::string_view buffer = zipWriter.getBuffer();

	if (buffer.size()) {
		target.resize(buffer.size());
		target.write((const uint8*)buffer.data(), buffer.size());
		return true;
	}

	return false;
}
