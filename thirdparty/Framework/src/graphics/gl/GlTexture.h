#pragma once

#include "foundation/ResourceProvider.h"
#include "graphics/Texture.h"

namespace fw {
	class GlTexture : public Texture {
	private:
		uint32 _handle = 0;

	public:
		GlTexture(uint32 handle, const TextureDesc& desc): Texture(desc), _handle(handle) {}
		~GlTexture();

		uint32 getGlHandle() const {
			return _handle;
		}

		friend class GlTextureProvider;
	};

	class GlTextureProvider : public TypedResourceProvider<Texture> {
	private:
		std::shared_ptr<Texture> _default;

	public:
		GlTextureProvider();
		~GlTextureProvider() = default;

		void getExtensions(std::vector<std::string>& target) override { target.insert(target.begin(), { ".png", ".jpg" }); }

		std::shared_ptr<Resource> getDefault() override { return _default; }

		std::shared_ptr<Resource> load(std::string_view uri) override;

		std::shared_ptr<Resource> create(const TextureDesc& desc, std::vector<std::string>& deps) override;

		bool update(Texture& texture, const TextureDesc& desc) override;
	};
}
