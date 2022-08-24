#pragma once

#include <bx/allocator.h>

#include "foundation/ResourceProvider.h"
#include "graphics/Texture.h"
#include "graphics/bgfx/BgfxResource.h"

namespace fw::engine {
	using BgfxTexture = BgfxResource<Texture, bgfx::TextureHandle>;

	class BgfxTextureProvider : public TypedResourceProvider<Texture> {
	private:
		bx::DefaultAllocator _alloc;
		std::shared_ptr<Texture> _default;

	public:
		BgfxTextureProvider();
		~BgfxTextureProvider() = default;

		std::shared_ptr<Resource> getDefault() { return _default; }

		std::shared_ptr<Resource> load(std::string_view uri) override;

		std::shared_ptr<Resource> create(const TextureDesc& desc, std::vector<std::string>& deps) override;

		bool update(Texture& texture, const TextureDesc& desc) override;
	};
}
