#include "BgfxTexture.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/error.h>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"

using namespace fw;
using namespace fw::engine;

BgfxTextureProvider::BgfxTextureProvider() {
	TextureDesc desc = {
		.dimensions = { 2, 2 },
		.depth = 4,
		.data = { 
			0xFF, 0xFF, 0xFF, 0xFF, 
			0xFF, 0xFF, 0xFF, 0xFF, 
			0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF
		}
	};

	std::vector<std::string> deps;
	_default = std::static_pointer_cast<Texture>(create(desc, deps));
}

std::shared_ptr<Resource> BgfxTextureProvider::load(std::string_view uri) {
	if (fs::exists(uri)) {
		uintmax_t fileSize = fs::file_size(uri);
		std::vector<std::byte> fileData = FsUtil::readFile(uri);

		if (fileData.size() > 0) {
			bx::Error err;

			bimg::ImageContainer* imageContainer = bimg::imageParse(
				(bx::AllocatorI*)&_alloc,
				(const void*)fileData.data(),
				(uint32_t)fileData.size()
			);

			if (imageContainer) {
				assert(imageContainer->m_format == bgfx::TextureFormat::RGB8 || imageContainer->m_format == bgfx::TextureFormat::RGBA8);

				const bgfx::Memory* mem = bgfx::copy(imageContainer->m_data, imageContainer->m_size);

				bgfx::TextureHandle handle = bgfx::createTexture2D(
					uint16_t(imageContainer->m_width),
					uint16_t(imageContainer->m_height),
					imageContainer->m_numMips > 1,
					imageContainer->m_numLayers,
					bgfx::TextureFormat::Enum(imageContainer->m_format),
					0,
					mem
				);

				bgfx::setName(handle, uri.data());

				TextureDesc desc = {
					.dimensions = Dimension { (int32)imageContainer->m_width, (int32)imageContainer->m_height },
					.depth = imageContainer->m_format == bgfx::TextureFormat::RGB8 ? 3u : 4u
				};

				return std::make_shared<BgfxTexture>(handle);
			} else {
				spdlog::error("Failed to load texture at {}: {}", uri, err.getMessage().getPtr());
			}
		} else {
			spdlog::error("Failed to load texture at {}: The file is empty", uri);
		}
	} else {
		spdlog::error("Failed to load texture at {}: The file does not exist", uri);
	}

	return _default;
}

std::shared_ptr<Resource> BgfxTextureProvider::create(const TextureDesc& desc, std::vector<std::string>& deps) {
	bgfx::TextureFormat::Enum format = desc.depth == 3 ? bgfx::TextureFormat::RGB8 : bgfx::TextureFormat::RGBA8;
	const bgfx::Memory* mem = nullptr;

	if (desc.data.size()) {
		assert(desc.data.size() == desc.dimensions.w * desc.dimensions.h * desc.depth);
		mem = bgfx::copy(desc.data.data(), (uint32)desc.data.size());
	}

	bgfx::TextureHandle handle = bgfx::createTexture2D(desc.dimensions.w, desc.dimensions.h, false, 1, format, BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);

	if (mem) {
		bgfx::updateTexture2D(handle, 0, 0, 0, 0, desc.dimensions.w, desc.dimensions.h, mem);
	}

	//bgfx::setName(handle, uri.data());

	return std::make_shared<BgfxTexture>(handle);
}

bool BgfxTextureProvider::update(Texture& resource, const TextureDesc& desc) {
	assert(desc.data.size());
	assert(desc.data.size() == desc.dimensions.w * desc.dimensions.h * desc.depth);

	BgfxTexture& texture = (BgfxTexture&)resource;
	const bgfx::Memory* mem = bgfx::copy(desc.data.data(), (uint32)desc.data.size());

	bgfx::updateTexture2D(texture.getBgfxHandle(), 0, 0, 0, 0, desc.dimensions.w, desc.dimensions.h, mem);

	return true;
}
