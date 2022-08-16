#include "FtglFont.h"

#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"

using namespace rp;
using namespace rp::engine;

FtglFont::~FtglFont() {
	ftgl::texture_font_delete(_font);
	ftgl::texture_atlas_delete(_atlas);
}

std::shared_ptr<Resource> FtglFontProvider::load(std::string_view uri) {
	if (fs::exists(uri)) {
		uintmax_t fileSize = fs::file_size(uri);
		std::vector<std::byte> fileData = FsUtil::readFile(uri);

		if (fileData.size() > 0) {
			Dimension atlasSize(512, 512);

			ftgl::texture_atlas_t* atlas = ftgl::texture_atlas_new(atlasSize.w, atlasSize.h, 3);
			ftgl::texture_font_t* font = ftgl::texture_font_new_from_memory(atlas, 16, fileData.data(), fileData.size());
			size_t missed = ftgl::texture_font_load_glyphs(font, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+ ");

			if (missed > 0) {
				spdlog::error("Missed {} glyphs when loading {}", missed, uri);
			}

			const uint32 dataSize = atlasSize.w * atlasSize.h * 3;
			std::vector<uint8> data(dataSize);
			memcpy(data.data(), atlas->data, dataSize);

			TextureHandle texture = _resourceManager.create<Texture>(TextureDesc {
				.dimensions = atlasSize,
				.depth = 3,
				.data = std::move(data)
			});

			return std::make_shared<FtglFont>(texture, atlas, font);
		} else {
			spdlog::error("Failed to load font at {}: The file contains no data", uri);
		}
	} else {
		spdlog::error("Failed to load font at {}: The file does not exist", uri);
	}

	return nullptr;
}

std::shared_ptr<Resource> FtglFontProvider::create(const FontDesc& desc, std::vector<std::string>& deps) {
	return nullptr;
}

bool FtglFontProvider::update(Font& resource, const FontDesc& desc) {
	return false;
}
