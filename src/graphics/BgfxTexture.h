#pragma once

#include <bx/allocator.h>
#include <bgfx/bgfx.h>

#include "platform/Types.h"
#include "RpMath.h"
#include <unordered_map>
#include <entt/resource/resource.hpp>
#include <entt/core/hashed_string.hpp>

#include <filesystem>

#include "foundation/ResourceHandle.h"
#include "foundation/ResourceProvider.h"

namespace rp::engine {
	struct TextureDesc {
		Dimension dimensions;
		uint32 depth;
	};

	class Texture : public Resource {
	private:
		TextureDesc _desc;

	public:
		using DescT = TextureDesc;

		Texture(const TextureDesc& desc): Resource(entt::type_id<Texture>()), _desc(desc) {}

		const TextureDesc& getDesc() const {
			return _desc;
		}
	};

	class BgfxTexture : public Texture {
	private:
		bgfx::TextureHandle _handle;

	public:
		BgfxTexture(const TextureDesc& desc, bgfx::TextureHandle handle): Texture(desc), _handle(handle) {}
		~BgfxTexture() { bgfx::destroy(_handle); }

		bgfx::TextureHandle getBgfxHandle() const {
			return _handle;
		}
	};

	class BgfxTextureProvider : public TypedResourceProvider<Texture> {
	private:
		bx::DefaultAllocator _alloc;

	public:
		std::unique_ptr<Resource> load(std::string_view uri) override;

		std::unique_ptr<Resource> create(std::string_view uri, const TextureDesc& desc) override;
	};

	/*struct TextureAtlas {
		entt::resource<Texture> texture;
		std::unordered_map<entt::id_type, Rect> tiles;

		const Rect* getTileArea(std::string_view tileUri) const {
			auto found = tiles.find(entt::hashed_string(tileUri.data(), tileUri.size()));
			if (found != tiles.end()) {
				return &found->second;
			}

			return nullptr;
		}
	};

	struct TextureLoader final {
	private:
		bx::DefaultAllocator _alloc;

	public:
		using result_type = std::shared_ptr<Texture>;

		result_type operator()(const std::filesystem::path& path) const;

		result_type operator()(uint32 w, uint32 h, uint32 d, const char* data) const;
	};

	struct TextureAtlasLoader final {
		using result_type = std::shared_ptr<TextureAtlas>;

		result_type operator()(const std::filesystem::path& path) const { return nullptr; }

		result_type operator()(const entt::resource<Texture>& texture, const std::unordered_map<entt::id_type, Rect>& tiles) const {
			return std::make_shared<TextureAtlas>(TextureAtlas{
				.texture = texture,
				.tiles = tiles
			});
		}
	};*/
}
