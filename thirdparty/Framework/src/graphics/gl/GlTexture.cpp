#include "GlTexture.h"

#include <glad/gl.h>
#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "foundation/FsUtil.h"

namespace fw::engine {
	GLenum getGlFormat(int32 compCount) {
		switch (compCount) {
			case 1: return GL_RED;
			case 2: return GL_RG;
			case 3: return GL_RGB;
			case 4: return GL_RGBA;
		}

		return GL_INVALID_ENUM;
	}

	GlTexture::~GlTexture() {
		glDeleteTextures(1, &_handle);
		_handle = 0;
	}

	GlTextureProvider::GlTextureProvider() {
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

	std::shared_ptr<Resource> GlTextureProvider::load(std::string_view uri) {
		if (fs::exists(uri)) {
			uintmax_t fileSize = fs::file_size(uri);
			std::vector<std::byte> fileData = fw::FsUtil::readFile(uri);

			if (fileData.size() > 0) {
				Dimension dim;
				int32 comp;
				stbi_uc* imageData = stbi_load_from_memory((const stbi_uc*)fileData.data(), (int)fileData.size(), &dim.w, &dim.h, &comp, 0);

				if (imageData) {
					GLenum format = getGlFormat(comp);

					uint32 texture;
					glGenTextures(1, &texture);
					glBindTexture(GL_TEXTURE_2D, texture);

					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

					glTexImage2D(GL_TEXTURE_2D, 0, format, dim.w, dim.h, 0, format, GL_UNSIGNED_BYTE, imageData);
					//glGenerateMipmap(GL_TEXTURE_2D);

					stbi_image_free(imageData);

					TextureDesc desc = {
						.dimensions = dim,
						.depth = (uint32)comp
					};

					return std::make_shared<GlTexture>(texture, desc);
				} else {
					spdlog::error("Failed to load texture at {}: Failed to load texture with STB image");
				}
			} else {
				spdlog::error("Failed to load texture at {}: The file is empty", uri);
			}
		} else {
			spdlog::error("Failed to load texture at {}: The file does not exist", uri);
		}

		return _default;
	}

	std::shared_ptr<Resource> GlTextureProvider::create(const TextureDesc& desc, std::vector<std::string>& deps) {
		GLenum format = getGlFormat((int32)desc.depth);

		uint32 texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTexImage2D(GL_TEXTURE_2D, 0, format, desc.dimensions.w, desc.dimensions.h, 0, format, GL_UNSIGNED_BYTE, desc.data.data());
		//glGenerateMipmap(GL_TEXTURE_2D);

		return std::make_shared<GlTexture>(texture, desc);
	}

	bool GlTextureProvider::update(Texture& resource, const TextureDesc& desc) {
		assert(desc.data.size());
		assert(desc.data.size() == desc.dimensions.w * desc.dimensions.h * desc.depth);

		GLenum format = getGlFormat((int32)desc.depth);

		GlTexture& texture = (GlTexture&)resource;
		texture._desc = desc;

		glBindTexture(GL_TEXTURE_2D, texture.getGlHandle());
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, desc.dimensions.w, desc.dimensions.h, format, GL_UNSIGNED_BYTE, desc.data.data());

		return true;
	}
}
