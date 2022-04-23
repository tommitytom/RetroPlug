#include "ProjectExporter.h"

#include "core/ProjectSerializer.h"
#include "util/zipp.h"

using namespace rp;

bool ProjectExporter::exportProject(Project& project, Uint8Buffer& target) {
	zipp::Writer zipWriter({ .method = zipp::CompressionMethod::Deflate });
	
	std::string fileData = ProjectSerializer::serialize(project.getState(), project.getSystems());
	if (fileData.size()) {
		zipWriter.add("project.rplg.lua", fileData);
		zipWriter.close();
		std::string_view buffer = zipWriter.getBuffer();

		if (buffer.size()) {
			target.resize(buffer.size());
			target.write((const uint8*)buffer.data(), buffer.size());
			return true;
		}		
	}

	return false;
}
