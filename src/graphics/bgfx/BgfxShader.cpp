#include "BgfxShader.h"

#include <spdlog/spdlog.h>

#include "platform/Types.h"
#include "foundation/FsUtil.h"

using namespace rp;
using namespace rp::engine;

std::shared_ptr<Resource> BgfxShaderProvider::load(std::string_view uri) {
	if (fs::exists(uri)) {
		std::vector<std::byte> fileData = FsUtil::readFile(uri);

		if (fileData.size() > 0) {
			const bgfx::Memory* mem = bgfx::copy(fileData.data(), (uint32)fileData.size());
			bgfx::ShaderHandle handle = bgfx::createShader(mem);
			return std::make_shared<BgfxShader>(handle);
		} else {
			spdlog::error("Failed to load shader at {}, failed to open the file", uri);
		}
	} else {
		spdlog::error("Failed to load shader at {}, the file does not exist", uri);
	}

	return nullptr;
}

std::shared_ptr<Resource> BgfxShaderProvider::create(const ShaderDesc& desc, std::vector<std::string>& deps) {
	const bgfx::Memory* mem = bgfx::makeRef(desc.data, desc.size);
	bgfx::ShaderHandle handle = bgfx::createShader(mem);
	return std::make_shared<BgfxShader>(handle);
}
