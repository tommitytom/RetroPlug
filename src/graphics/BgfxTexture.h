#pragma once

#include <bx/allocator.h>
#include <bgfx/bgfx.h>

#include "platform/Types.h"
#include "RpMath.h"
#include <unordered_map>
#include <entt/resource/resource.hpp>
#include <entt/core/hashed_string.hpp>

#include <filesystem>

namespace rp::engine {
	struct Texture {
		bgfx::TextureHandle handle;
		Dimension dimensions;
		uint32 depth;
	};

	struct TextureAtlas {
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
	};
}
