#include "BgfxTexture.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/error.h>
#include <spdlog/spdlog.h>

#include <fstream>

using namespace rp;
using namespace rp::engine;

namespace fs = std::filesystem;

std::vector<std::byte> readFile(const fs::path& path) {
	std::vector<std::byte> target;
	std::ifstream f(path.lexically_normal(), std::ios::binary);

	//es_assert(f.is_open());
	if (!f.is_open()) {
		spdlog::warn("Failed to open {}", path.string());
		return target;
	}

	f.seekg(0, std::ios::end);
	std::streamoff size = f.tellg();
	f.seekg(0, std::ios::beg);

	target.resize((size_t)size);
	f.read((char*)target.data(), target.size());

	if (size == 0) {
		spdlog::warn("File is empty {}", path.string());
	}

	return target;
}

TextureLoader::result_type TextureLoader::operator()(const std::filesystem::path& path) const {
	if (fs::exists(path)) {
		uintmax_t fileSize = fs::file_size(path);
		
		std::vector<std::byte> fileData = readFile(path);
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

				bgfx::setName(handle, "LOADED");

				return std::make_shared<Texture>(Texture{
					.handle = handle,
					.dimensions = Dimension { (int32)imageContainer->m_width, (int32)imageContainer->m_height },
					.depth = imageContainer->m_format == bgfx::TextureFormat::RGB8 ? 3u : 4u
				});
			} else {
				spdlog::error("Failed to load texture at {}: {}", path.string(), err.getMessage().getPtr());
			}
		}
	}

	return nullptr;
}

TextureLoader::result_type TextureLoader::operator()(uint32 w, uint32 h, uint32 d, const char* data) const {
	bgfx::TextureFormat::Enum format = d == 3 ? bgfx::TextureFormat::RGB8 : bgfx::TextureFormat::RGBA8;
	const bgfx::Memory* mem = nullptr;

	if (data) {
		const uint32 dataSize = w * h * d;
		mem = bgfx::copy(data, dataSize);
	}

	bgfx::TextureHandle handle = bgfx::createTexture2D(w, h, false, 1, format, 0, mem);

	bgfx::setName(handle, "WHITE");

	return std::make_shared<Texture>(Texture{
		.handle = handle,
		.dimensions = Dimension { (int32)w, (int32)h },
		.depth = d
	});
}
