#pragma once

#include "foundation/Math.h"
#include "foundation/ResourceHandle.h"
#include "foundation/ResourceProvider.h"
#include "graphics/Texture.h"

namespace fw {
	struct TextureAtlasDesc {
		TextureHandle texture;
		std::vector<std::pair<std::string, Rect>> tiles;
	};

	struct TileArea {
		f32 top;
		f32 left;
		f32 bottom;
		f32 right;
	};

	struct TextureAtlasTile {
		TextureHandle handle;
		DimensionF dimensions;
		TileArea uvArea = TileArea{0, 0, 1, 1};
		std::string name;
	};

	class TextureAtlas final : public Resource {
	private:
		std::unordered_map<UriHash, TextureAtlasTile> _tiles;

	public:
		using DescT = TextureAtlasDesc;

		TextureAtlas(const TextureAtlasDesc& desc) : Resource(entt::type_id<TextureAtlas>()) {
			assert(desc.texture.isLoaded());
			DimensionF dimensions = (DimensionF)desc.texture.getDesc().dimensions;

			for (const auto& tile : desc.tiles) {
				_tiles[ResourceUtil::hashUri(tile.first)] = TextureAtlasTile {
					.handle = desc.texture,
					.dimensions = dimensions,
					.uvArea = {
						.top = tile.second.y / dimensions.h,
						.left = tile.second.x / dimensions.w,
						.bottom = tile.second.bottom() / dimensions.h,
						.right = tile.second.right() / dimensions.w
					},
					.name = tile.first
				};
			}
		}

		~TextureAtlas() = default;

		const TextureAtlasTile& getTile(UriHash hash) const {
			assert(_tiles.contains(hash));
			return _tiles.at(hash);
		}

		const TextureAtlasTile* findTile(UriHash hash) const {
			auto found = _tiles.find(hash);
			if (found != _tiles.end()) {
				return &found->second;
			}

			return nullptr;
		}
	};

	using TextureAtlasHandle = TypedResourceHandle<TextureAtlas>;

	class TextureAtlasProvider : public TypedResourceProvider<TextureAtlas> {
	public:
		std::shared_ptr<Resource> load(std::string_view uri) override { assert(false);  return nullptr; }

		std::shared_ptr<Resource> create(const TextureAtlasDesc& desc, std::vector<std::string>& deps) override {
			return std::make_shared<TextureAtlas>(desc);
		}
	};
}
