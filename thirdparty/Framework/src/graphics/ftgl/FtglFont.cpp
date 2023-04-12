#include "FtglFont.h"

#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>
#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "graphics/resources/Karla-Regular.h"
#include "graphics/resources/Roboto-Regular.h"

using namespace fw;


const std::string_view DEFAULT_CODEPOINTS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&:;*()[]_-+. ";

FtglFontFace::~FtglFontFace() {
	ftgl::texture_font_delete(_font);
	ftgl::texture_atlas_delete(_atlas);
}

std::shared_ptr<FontFace> createTextureFont(ResourceManager& resourceManager, const char* fontData, size_t fontDataSize, f32 fontSize, std::string_view codePoints, std::string_view name) {
	Dimension atlasSize(1024, 1024);

	if (codePoints.empty()) {
		codePoints = DEFAULT_CODEPOINTS;
	}

	ftgl::texture_atlas_t* atlas = ftgl::texture_atlas_new(atlasSize.w, atlasSize.h, 4);
	ftgl::texture_font_t* font = ftgl::texture_font_new_from_memory(atlas, fontSize, fontData, fontDataSize);
	size_t missed = ftgl::texture_font_load_glyphs(font, codePoints.data());

	if (missed > 0) {
		spdlog::error("Missed {} glyphs when loading {}", missed, name);
	}

	const uint32 dataSize = atlasSize.w * atlasSize.h * 4;
	std::vector<uint8> data(dataSize);
	memcpy(data.data(), atlas->data, dataSize);

	TextureHandle texture = resourceManager.create<Texture>(fmt::format("fonts/{}/{}/texture", name, fontSize), TextureDesc{
		.dimensions = atlasSize,
		.depth = 4,
		.data = std::move(data)
	});

	return std::make_shared<FtglFontFace>(texture, atlas, font);
}

FtglFontFaceProvider::FtglFontFaceProvider(ResourceManager& resourceManager) : _resourceManager(resourceManager) {
	_default = createTextureFont(_resourceManager, (const char*)Karla_Regular, Karla_Regular_len, 16.0f, DEFAULT_CODEPOINTS, "Karla-Regular");
	assert(_default);
}

std::shared_ptr<Resource> FtglFontFaceProvider::load(std::string_view uri) {
	size_t lastSlash = uri.find_last_of("/\\");
	assert(lastSlash != std::string::npos);

	std::string_view fontUri = uri.substr(0, lastSlash);
	std::string_view sizeStr = uri.substr(lastSlash + 1);
	f32 size = (f32)::atof(sizeStr.data());

	std::vector<std::string> deps;
	return create(FontFaceDesc{
		.font = std::string(fontUri),
		.size = size
	}, deps);
}

std::shared_ptr<Resource> FtglFontFaceProvider::create(const FontFaceDesc& desc, std::vector<std::string>& deps) {
	FontHandle fontHandle = _resourceManager.load<Font>(desc.font);

	if (fontHandle.isValid() && fontHandle.isLoaded()) {
		const Font& font = fontHandle.getResource();
		return createTextureFont(_resourceManager, (const char*)font.getData().data(), font.getData().size(), desc.size, desc.codePoints, desc.font);
	}

	return _default;
}

bool FtglFontFaceProvider::update(FontFace& resource, const FontFaceDesc& desc) {
	return false;
}
