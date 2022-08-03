#pragma once

#include <filesystem>
#include <unordered_set>
#include <vector>

#include <entt/core/hashed_string.hpp>
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>

#include "RpMath.h"
#include "graphics/BgfxTexture.h"

using namespace entt::literals;

namespace ftgl {
	struct texture_atlas_t;
	struct texture_font_t;
}

namespace rp::engine {
	struct CanvasVertex {
		PointF pos;
		uint32 abgr;
		PointF uv;
	};

	enum class RenderPrimitive {
		Points,
		LineList,
		LineLoop,
		LineStrip,
		Triangles,
		TriangleStrip,
		TriangleFan,
	};

	struct Tile {
		f32 top;
		f32 left;
		f32 bottom;
		f32 right;
	};

	struct CanvasSurface {
		RenderPrimitive primitive = RenderPrimitive::Triangles;
		entt::resource<Texture> texture;
		size_t indexOffset = 0;
		size_t indexCount = 0;
		uint32 viewId = 0;
		Rect viewArea;
		f32 zoom = 1.0f;
	};

	struct TextureRenderDesc {
		entt::resource<Texture> textureHandle;
		RectT<f32> textureUv;
		RectT<f32> area;
		Color4F color;
	};

	struct CanvasGeometry {
		std::vector<CanvasVertex> vertices;
		std::vector<uint32> indices;
		std::vector<CanvasSurface> surfaces;
	};

	class Canvas {
	private:
		CanvasGeometry _geom;

		Rect _viewPort;
		Rect _res;
		f32 _pixelRatio = 1.0f;

		uint32 _viewId;

		entt::resource<Texture> _whiteTexture;

		entt::resource_cache<Texture, TextureLoader> _textureCache;
		entt::resource_cache<TextureAtlas, TextureAtlasLoader> _textureAtlasCache;

		std::unordered_map<entt::id_type, std::pair<entt::resource<Texture>, Tile>> _tileLookup;

		PointF _offset = { 0, 0 };
		PointF _scale = { 1, 1 };
		f32 _rotation = 0.0f;
		Mat3x3 _transform;

		ftgl::texture_atlas_t* _atlas = nullptr;
		ftgl::texture_font_t* _font = nullptr;

		bool _lineAA = false;

		std::unordered_set<entt::id_type> _invalidUris;

	public:
		Canvas(uint32 viewId = 0);
		~Canvas();

		void clear() {
			_geom.indices.clear();
			_geom.vertices.clear();
			_geom.surfaces.clear();
			clearTransform();
		}

		Dimension getDimensions() const {
			return _res.dimensions;
		}

		const CanvasGeometry& getGeometry() const {
			return _geom;
		}

		void setViewId(uint32 viewId) {
			_viewId = viewId;
		}

		void setTransform(PointF position, PointF scale = { 1, 1 }, f32 rotation = 0.0f) {
			_offset = position;
			_scale = scale;
			_rotation = rotation;
			_transform = Mat3x3::translation(position);
		}

		Canvas& setTransform(const Mat3x3& transform) {
			_transform = transform;
			return *this;
		}

		void clearTransform() {
			_offset = { 0, 0 };
			_scale = { 1, 1 };
			_rotation = 0.0f;
			_transform = Mat3x3();
		}

		entt::resource<Texture> loadTexture(const std::filesystem::path& filePath);

		entt::resource<TextureAtlas> createTextureAtlas(std::string_view uri, const entt::resource<Texture>& texture, const std::unordered_map<entt::id_type, Rect>& tiles);

		std::optional<std::pair<entt::resource<Texture>, Tile>> getTile(entt::id_type uriHash) const {
			auto found = _tileLookup.find(uriHash);

			if (found != _tileLookup.end()) {
				return found->second;
			}

			return std::nullopt;
		}

		//CanvasTextureHandle loadTexture(const std::filesystem::path& filePath);

		void beginRender(Dimension res, f32 pixelRatio);

		void endRender();

		Canvas& translate(PointF amount);

		Canvas& polygon(const PointF* points, uint32 count);

		Canvas& points(const PointF* points, uint32 count);

		Canvas& setScale(f32 scaleX, f32 scaleY);

		Canvas& fillRect(const RectT<f32>& area, const Color4F& color);

		Canvas& fillRect(const Rect& area, const Color4F& color) { fillRect((RectF)area, color); }

		Canvas& texture(const TextureRenderDesc& desc);

		Canvas& texture(entt::id_type uriHash, const RectT<f32>& area, const Color4F& color);

		Canvas& texture(std::string_view uri, const RectT<f32>& area, const Color4F& color) {
			entt::id_type uriHash = entt::hashed_string(uri.data(), uri.size());
			return texture(uriHash, area, color);
		}

		Canvas& texture(const entt::resource<Texture>& texture, const RectT<f32>& area, const Color4F& color);

		Canvas& texture(const entt::resource<Texture>& texture, const Rect& textureArea, const RectT<f32>& area, const Color4F& color);

		Canvas& strokeRect(const RectT<f32>& area, const Color4F& color);

		Canvas& text(f32 x, f32 y, std::string_view text, const Color4F& color);

		Canvas& circle(const PointF& pos, f32 radius, uint32 segments = 32, const Color4F& color = Color4F(1,1,1,1));

		Canvas& line(const PointF& from, const PointF& to, const Color4F& color);

	private:
		void checkSurface(RenderPrimitive primitive, const entt::resource<Texture>& texture);

		inline uint32 writeVertex(const PointF& pos, uint32 color) {
			_geom.vertices.push_back(CanvasVertex{ _transform * pos, color, 0, 0 });
			return (uint32)_geom.vertices.size() - 1;
		}

		inline void writeTriangleIndices(uint32 v1, uint32 v2, uint32 v3) {
			_geom.indices.insert(_geom.indices.end(), { v1, v2, v3 });
			_geom.surfaces.back().indexCount += 3;
		}

		entt::resource<Texture> createWhiteTexture(uint32 w, uint32 h) {
			const uint32 size = w * h * 4;
			std::vector<char> data(size);
			
			memset(data.data(), 0xFF, size);
			return _textureCache.load("white"_hs, w, h, 4, data.data()).first->second;
		}
	};
}
