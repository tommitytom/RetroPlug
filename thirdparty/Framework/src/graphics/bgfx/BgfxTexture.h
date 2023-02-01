#pragma once

#include <bx/allocator.h>
#include <bgfx/bgfx.h>

#include "foundation/ResourceProvider.h"
#include "graphics/Texture.h"

namespace fw {
	class BgfxTexture : public Texture {
	private:
		bgfx::TextureHandle _handle = { bgfx::kInvalidHandle };

	public:
		BgfxTexture(bgfx::TextureHandle handle, const TextureDesc& desc): Texture(desc), _handle(handle) {}
		~BgfxTexture() {
			if (bgfx::isValid(_handle)) {
				bgfx::destroy(_handle);
			}
		}

		bgfx::TextureHandle getBgfxHandle() const {
			return _handle;
		}

		friend class BgfxTextureProvider;
	};

	class BgfxTextureProvider : public TypedResourceProvider<Texture> {
	private:
		bx::DefaultAllocator _alloc;
		std::shared_ptr<Texture> _default;

	public:
		BgfxTextureProvider();
		~BgfxTextureProvider() = default;

		std::shared_ptr<Resource> getDefault() override { return _default; }

		std::shared_ptr<Resource> load(std::string_view uri) override;

		std::shared_ptr<Resource> create(const TextureDesc& desc, std::vector<std::string>& deps) override;

		bool update(Texture& texture, const TextureDesc& desc) override;
	};
}
