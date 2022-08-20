#pragma once

#include <filesystem>
#include <span>
#include <unordered_set>
#include <vector>

#include <entt/core/hashed_string.hpp>
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>

#include "RpMath.h"
#include "foundation/ResourceHandle.h"
#include "foundation/ResourceManager.h"
#include "graphics/Font.h"
#include "graphics/FontManager.h"
#include "graphics/ShaderProgram.h"
#include "graphics/Texture.h"
#include "graphics/TextureAtlas.h"

using namespace entt::literals;

namespace ftgl {
	struct texture_atlas_t;
	struct texture_font_t;
}

namespace rp::engine {
	enum TextAlignFlags {
		Left = 1 << 0,
		Center = 1 << 1,
		Right = 1 << 2,
		Top = 1 << 3,
		Middle = 1 << 4,
		Bottom = 1 << 5,
		Baseline = 1 << 6
	};

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
		RectF textureUv;
		RectF area;
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

		std::string _fontName = "Karla-Regular";
		f32 _fontSize = 16.0f;
		FontHandle _font;

		PointF _translation = { 0, 0 };
		PointF _scale = { 1, 1 };
		f32 _rotation = 0.0f;
		Mat3x3 _transform;
		Color4F _color;

		bool _lineAA = false;

		std::unordered_set<entt::id_type> _invalidUris;

		ResourceManager& _resourceManager;
		FontManager& _fontManager;

		uint32 _textAlign = TextAlignFlags::Left | TextAlignFlags::Baseline;

	public:
		Canvas(ResourceManager& resourceManager, FontManager& fontManager, uint32 viewId = 0);
		~Canvas() = default;

		void destroy() {
			_defaultTexture = nullptr;
			_defaultProgram = nullptr;
			_defaultFont = nullptr;
			_font = nullptr;
			_invalidUris.clear();
			_geom = CanvasGeometry();
		}

		void setFont(std::string_view font, f32 size) {
			_fontName = std::string(font);
			_fontSize = size;
			updateFont();
		}

		Canvas& setTextAlign(uint32 align) {
			_textAlign = align;
			return *this;
		}

		void updateFont() {
			assert(_fontName.size());
			PointF scale = _transform.getScale();
			_font = loadFont(_fontName, _fontSize * std::max(scale.x, scale.y));
		}

		FontHandle loadFont(std::string_view name, f32 size) {
			return _resourceManager.load<Font>(fmt::format("{}/{}", name, size));
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

		Canvas& setTransform(PointF position, PointF scale = { 1, 1 }, f32 rotation = 0.0f) {
			_translation = position;
			_scale = scale;
			_rotation = rotation;
			updateTransform();
			updateFont();
			return *this;
		}

		void updateTransform() {
			_transform = Mat3x3::trs(_translation, _rotation, _scale);
		}

		Canvas& setTransform(const Mat3x3& transform) {
			_transform = transform;
			updateFont();
			return *this;
		}

		void clearTransform() {
			_translation = { 0, 0 };
			_scale = { 1, 1 };
			_rotation = 0.0f;
			_transform = Mat3x3();
			updateFont();
		}

		Canvas& setColor(const Color4F& color) {
			_color = color;
			return *this;
		}

		void beginRender(Dimension res, f32 pixelRatio);

		void endRender();

		DimensionF measureText(std::string_view text) { return this->measureText(text, _fontName, _fontSize); }

		DimensionF measureText(std::string_view text, std::string_view font, f32 size);

		Canvas& setProgram(ShaderProgramHandle program);

		Canvas& clearProgram();

		Canvas& translate(PointF amount);

		Canvas& scale(PointF amount);

		Canvas& rotate(f32 amount);

		Canvas& setTranslation(PointF translation);

		Canvas& setScale(PointF scale);

		Canvas& setRotation(f32 rotation);

		Canvas& polygon(const PointF* points, uint32 count);

		Canvas& points(const PointF* points, uint32 count);

		template <const size_t PointCount>
		Canvas& points(const std::array<PointF, PointCount>& points) { return this->points(points.data(), points.size()); }

		Canvas& fillRect(const RectF& area, const Color4F& color);

		Canvas& fillRect(const RectF& area) { return this->fillRect(area, _color); }

		Canvas& fillRect(const Rect& area, const Color4F& color) { return this->fillRect((RectF)area, color); }

		Canvas& fillRect(const Rect& area) { return this->fillRect((RectF)area, _color); }

		Canvas& strokeRect(const RectF& area, const Color4F& color);

		Canvas& texture(const TextureRenderDesc& desc);

		Canvas& texture(const TextureHandle& texture, const RectF& area, const Color4F& color);

		Canvas& texture(const TextureHandle& texture, const RectF& area) { return this->texture(texture, area, _color); }

		Canvas& texture(const TextureHandle& texture, const RectF& area, const TileArea& uvArea, const Color4F& color);

		Canvas& texture(const TextureHandle& texture, const RectF& area, const TileArea& uvArea) { return this->texture(texture, area, uvArea, _color); }

		Canvas& text(const PointF& pos, std::string_view text, const Color4F& color);

		Canvas& text(f32 x, f32 y, std::string_view text) { return this->text({ x, y }, text, _color); }

		Canvas& text(f32 x, f32 y, std::string_view text, const Color4F& color) { return this->text({ x, y }, text, color); }

		Canvas& circle(const PointF& pos, f32 radius) { return this->circle(pos, radius, _color); }

		Canvas& circle(const PointF& pos, f32 radius, const Color4F& color);

		Canvas& line(const PointF& from, const PointF& to, const Color4F& color);

		Canvas& line(const PointF& from, const PointF& to) { return line(from, to, _color); }

		Canvas& lines(std::span<PointF> points, const Color4F& color);

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
