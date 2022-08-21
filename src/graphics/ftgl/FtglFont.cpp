#include "FtglFont.h"

#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "graphics/resources/Karla-Regular.h"

using namespace rp;
using namespace rp::engine;

FtglFont::~FtglFont() {
	ftgl::texture_font_delete(_font);
	ftgl::texture_atlas_delete(_atlas);
}

std::shared_ptr<Font> createTextureFont(ResourceManager& resourceManager, const char* fontData, size_t fontDataSize, f32 fontSize, std::string_view name) {
	Dimension atlasSize(1024, 1024);

	ftgl::texture_atlas_t* atlas = ftgl::texture_atlas_new(atlasSize.w, atlasSize.h, 3);
	ftgl::texture_font_t* font = ftgl::texture_font_new_from_memory(atlas, fontSize, fontData, fontDataSize);
	size_t missed = ftgl::texture_font_load_glyphs(font, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()[]_-+. ");

	if (missed > 0) {
		spdlog::error("Missed {} glyphs when loading {}", missed, name);
	}

	const uint32 dataSize = atlasSize.w * atlasSize.h * 3;
	std::vector<uint8> data(dataSize);
	memcpy(data.data(), atlas->data, dataSize);

	TextureHandle texture = resourceManager.create<Texture>(fmt::format("fonts/Karla-Regular/{}/texture", fontSize), TextureDesc{
		.dimensions = atlasSize,
		.depth = 3,
		.data = std::move(data)
	});

	return std::make_shared<FtglFont>(texture, atlas, font);
}

FtglFontProvider::FtglFontProvider(ResourceManager& resourceManager) : _resourceManager(resourceManager) {
	_default = createTextureFont(_resourceManager, (const char*)Karla_Regular, Karla_Regular_len, 16.0f, "Karla-Regular");
	assert(_default);
}

std::shared_ptr<Resource> FtglFontProvider::load(std::string_view uri) {
	size_t lastSlash = uri.find_last_of('/');
	std::string_view path = uri.substr(0, lastSlash);
	std::string_view sizeStr = uri.substr(lastSlash + 1);

	if (fs::exists(path)) {
		uintmax_t fileSize = fs::file_size(path);
		std::vector<std::byte> fileData = FsUtil::readFile(path);

		if (fileData.size() > 0) {
			f32 size = (f32)::atof(sizeStr.data());
			return createTextureFont(_resourceManager, (const char*)fileData.data(), fileData.size(), size, path);
		} else {
			spdlog::error("Failed to load font at {}: The file contains no data", path);
		}
	} else {
		spdlog::error("Failed to load font at {}: The file does not exist", path);
	}

	return _default;
}

std::shared_ptr<Resource> FtglFontProvider::create(const FontDesc& desc, std::vector<std::string>& deps) {
	return nullptr;
}

bool FtglFontProvider::update(Font& resource, const FontDesc& desc) {
	return false;
}
