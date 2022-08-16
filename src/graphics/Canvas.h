#pragma once

#include <filesystem>
#include <unordered_set>
#include <vector>

#include <entt/core/hashed_string.hpp>
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>

#include "RpMath.h"
#include "foundation/ResourceHandle.h"
#include "graphics/Font.h"
#include "graphics/ShaderProgram.h"
#include "graphics/Texture.h"
#include "graphics/TextureAtlas.h"

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

	struct CanvasSurface {
		RenderPrimitive primitive = RenderPrimitive::Triangles;
		TextureHandle texture;
		ShaderProgramHandle program;
		size_t indexOffset = 0;
		size_t indexCount = 0;
		uint32 viewId = 0;
		Rect viewArea;
		f32 zoom = 1.0f;
	};

	struct TextureRenderDesc {
		TextureHandle textureHandle;
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

		TextureHandle _defaultTexture;
		ShaderProgramHandle _defaultProgram;
		FontHandle _defaultFont;

		FontHandle _font;

		PointF _offset = { 0, 0 };
		PointF _scale = { 1, 1 };
		f32 _rotation = 0.0f;
		Mat3x3 _transform;
		Color4F _color;

		bool _lineAA = false;

		std::unordered_set<entt::id_type> _invalidUris;

	public:
		Canvas(uint32 viewId = 0);
		~Canvas();

		void setFont(FontHandle font) {
			_font = font;
		}

		void clear() {
			_geom.indices.clear();
			_geom.vertices.clear();
			_geom.surfaces.clear();
			clearTransform();
		}

		void setDefaults(TextureHandle texture, ShaderProgramHandle program, FontHandle font) {
			_defaultTexture = texture;
			_defaultProgram = program;
			_defaultFont = font;
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

		Canvas& setColor(const Color4F& color) {
			_color = color;
			return *this;
		}

		void beginRender(Dimension res, f32 pixelRatio);

		void endRender();

		Canvas& setProgram(ShaderProgramHandle program);

		Canvas& clearProgram();

		Canvas& translate(PointF amount);

		Canvas& polygon(const PointF* points, uint32 count);

		Canvas& points(const PointF* points, uint32 count);

		template <const size_t PointCount>
		Canvas& points(const std::array<PointF, PointCount>& points) { return this->points(points.data(), points.size()); }

		Canvas& setScale(f32 scaleX, f32 scaleY);

		Canvas& fillRect(const RectT<f32>& area, const Color4F& color);

		Canvas& fillRect(const RectT<f32>& area) { return this->fillRect(area, _color); }

		Canvas& fillRect(const Rect& area, const Color4F& color) { return this->fillRect((RectF)area, color); }

		Canvas& fillRect(const Rect& area) { return this->fillRect((RectF)area, _color); }

		Canvas& texture(const TextureRenderDesc& desc);

		Canvas& texture(const TextureHandle& texture, const RectT<f32>& area, const Color4F& color);

		Canvas& texture(const TextureHandle& texture, const RectT<f32>& area) { return this->texture(texture, area, _color); }

		Canvas& texture(const TextureHandle& texture, const RectT<f32>& area, const TileArea& uvArea, const Color4F& color);

		Canvas& texture(const TextureHandle& texture, const RectT<f32>& area, const TileArea& uvArea) { return this->texture(texture, area, uvArea, _color); }

		Canvas& strokeRect(const RectT<f32>& area);

		Canvas& text(f32 x, f32 y, std::string_view text) { return this->text(x, y, text, _color); }

		Canvas& text(f32 x, f32 y, std::string_view text, const Color4F& color);

		Canvas& circle(const PointF& pos, f32 radius) { return this->circle(pos, radius, _color); }

		Canvas& circle(const PointF& pos, f32 radius, const Color4F& color);

		Canvas& line(const PointF& from, const PointF& to, const Color4F& color);

		Canvas& line(const PointF& from, const PointF& to) { return line(from, to, _color); }

	private:
		void checkSurface(RenderPrimitive primitive, const TextureHandle& texture, const ShaderProgramHandle& program);

		inline uint32 writeVertex(const PointF& pos, uint32 color) {
			_geom.vertices.push_back(CanvasVertex{ _transform * pos, color, 0, 0 });
			return (uint32)_geom.vertices.size() - 1;
		}

		inline void writeTriangleIndices(uint32 v1, uint32 v2, uint32 v3) {
			_geom.indices.insert(_geom.indices.end(), { v1, v2, v3 });
			_geom.surfaces.back().indexCount += 3;
		}
	};
}

using namespace rp::engine;
