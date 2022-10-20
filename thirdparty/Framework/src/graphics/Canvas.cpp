#include "Canvas.h"

#include <fstream>

#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>
#include <spdlog/spdlog.h>

#include "graphics/ftgl/FtglFont.h"

using namespace fw;
using namespace fw::engine;
namespace fs = std::filesystem;

constexpr PointF FIXED_VERTEX_OFFSET = PointF(0.00125f / 2.0f, 0);

uint32 toUint32Abgr(const Color4F& color) {
	return
		(static_cast<uint32>(color.a * 255.f) << 24) +
		(static_cast<uint32>(color.b * 255.f) << 16) +
		(static_cast<uint32>(color.g * 255.f) << 8) +
		(static_cast<uint32>(color.r * 255.f) << 0);
}

Canvas::Canvas(ResourceManager& resourceManager, FontManager& fontManager, uint32 viewId): _resourceManager(resourceManager), _fontManager(fontManager), _viewId(viewId) {

}

entt::id_type getUriHash(std::string_view uri) {
	return entt::hashed_string(uri.data(), uri.size());
}

entt::id_type getPathUriHash(const std::filesystem::path& filePath) {
	std::string uri = filePath.relative_path().make_preferred().replace_extension().string();
	return getUriHash(uri);
}

Canvas& Canvas::setProgram(ShaderProgramHandle program) {
	TextureHandle tex = getTopSurface().texture;
	checkSurface(getTopSurface().primitive, tex);
	return *this;
}

Canvas& Canvas::clearProgram() {
	return setProgram(_defaultProgram);
}

void Canvas::beginRender(Dimension res, f32 pixelRatio) {
	clear();

	_res.dimensions = res;
	_viewPort = Rect(0, 0, res.w, res.h);
	_projection = (RectF)_viewPort;
	_pixelRatio = pixelRatio;
	_font = _defaultFont;

	_geom.batches.push_back(CanvasBatch {
		.viewId = _viewId,
		.viewArea = _viewPort,
		.projection = _projection,
		.surfaces = {
			CanvasSurface{
				.primitive = RenderPrimitive::Triangles,
				.texture = _defaultTexture,
				.program = _defaultProgram,
			}
		}
	});
}

void Canvas::checkSurface(RenderPrimitive primitive, const TextureHandle& texture) {
	Rect scissor = _scissorStack.empty() ? Rect() : _scissorStack.back();
	CanvasSurface* topSurf = &getTopSurface();
	CanvasBatch& topBatch = _geom.batches.back();

	if (topBatch.scissor != scissor || topBatch.projection != _projection) {
		if (topBatch.surfaces.size() == 1 && topSurf->indexCount == 0) {
			topBatch.scissor = scissor;
			topBatch.projection = _projection;
		} else {
			_geom.batches.push_back(CanvasBatch{
				.viewId = _viewId + (uint32)_geom.batches.size(),
				.viewArea = _viewPort,
				.scissor = scissor,
				.projection = _projection,
				.surfaces = {
					CanvasSurface{
						.primitive = RenderPrimitive::Triangles,
						.texture = _defaultTexture,
						.program = _defaultProgram,
						.indexOffset = topSurf->indexOffset + topSurf->indexCount
					}
				}
			});
		}

		topSurf = &getTopSurface();
	}

	if (topSurf->indexCount == 0) {
		topSurf->primitive = primitive;
		topSurf->texture = texture;
		topSurf->program = _program;
	} else if (
		topSurf->primitive != primitive ||
		topSurf->texture != texture ||
		topSurf->program != _program
	) {
		_geom.batches.back().surfaces.push_back(CanvasSurface{
			.primitive = primitive,
			.texture = texture,
			.program = _program,
			.indexOffset = topSurf->indexOffset + topSurf->indexCount
		});
	}
}

void Canvas::endRender() {
	assert(_scissorStack.empty());
}

Canvas& Canvas::translate(PointF amount) {
	_translation += amount;
	updateTransform();
	return *this;
}

Canvas& Canvas::scale(PointF amount) {
	_scale *= amount;
	updateTransform();
	updateFont();
	return *this;
}

Canvas& Canvas::rotate(f32 amount) {
	_rotation += amount;
	updateTransform();
	return *this;
}

Canvas& Canvas::setTranslation(PointF translation) {
	_translation = translation;
	updateTransform();
	return *this;
}

Canvas& Canvas::setScale(PointF scale) {
	_scale = scale;
	updateTransform();
	updateFont();
	return *this;
}

Canvas& Canvas::setRotation(f32 rotation) {
	_rotation = rotation;
	updateTransform();
	return *this;
}

Canvas& Canvas::points(const PointF* points, uint32 count) {
	checkSurface(RenderPrimitive::Points, _defaultTexture);

	uint32 agbr = toUint32Abgr(Color4F(1, 1, 1, 1));
	uint32 v = (uint32)_geom.vertices.size();

	for (uint32 i = 0; i < count; ++i) {
		_geom.vertices.push_back(CanvasVertex{ _transform * points[i], agbr, 0, 0 });
		_geom.indices.push_back(v + i);
	}

	getTopSurface().indexCount += count;

	return *this;
}

Canvas& Canvas::polygon(const PointF* points, uint32 count) {
	checkSurface(RenderPrimitive::Triangles, _defaultTexture);

	uint32 agbr = toUint32Abgr(Color4F(1, 1, 1, 1));
	uint32 v = (uint32)_geom.vertices.size();

	for (uint32 i = 0; i < count; ++i) {
		_geom.vertices.push_back(CanvasVertex{ _transform * points[i], agbr, 0, 0 });
	}

	_geom.indices.insert(_geom.indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	getTopSurface().indexCount += 6;

	return *this;
}

Canvas& Canvas::line(const PointF& from, const PointF& to, const Color4F& color) {
	checkSurface(RenderPrimitive::LineList, _defaultTexture);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * (from + 0.5f), agbr, { 0, 0 } },
		CanvasVertex{ _transform * (to + 0.5f), agbr, { 0, 0 } }
	});

	_geom.indices.insert(_geom.indices.end(), { v + 0, v + 1 });

	getTopSurface().indexCount += 2;

	return *this;
}

Canvas& Canvas::lines(std::span<PointF> points, const Color4F& color) {
	assert(points.size() >= 2);

	checkSurface(RenderPrimitive::LineList, _defaultTexture);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	const PointF* data = points.data();

	for (uint32 i = 0; i < (uint32)points.size(); ++i) {
		_geom.vertices.push_back(CanvasVertex{ _transform * data[i], agbr, { 0, 0 } });

		if (i > 0) {
			_geom.indices.push_back(v + (i - 1));
			_geom.indices.push_back(v + i);
		}
	}

	getTopSurface().indexCount += (uint32)points.size() * 2 - 2;

	return *this;
}

Canvas& Canvas::fillCircle(const PointF& pos, f32 radius, const Color4F& color) {
	const uint32 cirleSegments = 32;
	checkSurface(RenderPrimitive::Triangles, _defaultTexture);

	uint32 agbr = toUint32Abgr(color);

	f32 step = PI2 / (f32)cirleSegments;
	uint32 centerIdx = writeVertex(pos, agbr);

	for (uint32 i = 0; i < cirleSegments; ++i) {
		f32 rad = (f32)i * step;
		PointF p = PointF(cos(rad), sin(rad)) * radius;
		uint32 v = writeVertex(p + pos, agbr);

		if (i < cirleSegments - 1) {
			writeTriangleIndices(centerIdx, v, v + 1);
		} else {
			writeTriangleIndices(centerIdx, v, centerIdx + 1);
		}
	}

	return *this;
}

Canvas& Canvas::fillRect(const RectF& area, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, _defaultTexture);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * area.position, agbr, { 0, 0 } },
		CanvasVertex{ _transform * area.topRight(), agbr, { 0, 0 } },
		CanvasVertex{ _transform * area.bottomRight(), agbr, { 0, 0 } },
		CanvasVertex{ _transform * area.bottomLeft(), agbr, { 0, 0 } },
	});

	_geom.indices.insert(_geom.indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	getTopSurface().indexCount += 6;

	return *this;
}

Canvas& Canvas::texture(const TextureRenderDesc& desc) {
	checkSurface(RenderPrimitive::Triangles, desc.textureHandle);

	uint32 agbr = toUint32Abgr(desc.color);
	uint32 v = (uint32)_geom.vertices.size();

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * desc.area.position, agbr, desc.textureUv.position },
		CanvasVertex{ _transform * desc.area.topRight(), agbr, desc.textureUv.topRight() },
		CanvasVertex{ _transform * desc.area.bottomRight(), agbr, desc.textureUv.bottomRight() },
		CanvasVertex{ _transform * desc.area.bottomLeft(), agbr, desc.textureUv.bottomLeft() },
	});

	_geom.indices.insert(_geom.indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	getTopSurface().indexCount += 6;

	return *this;
}

Canvas& Canvas::texture(const TextureHandle& texture, const RectF& area, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, texture);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * area.position, agbr, { 0, 0 } },
		CanvasVertex{ _transform * area.topRight(), agbr, { 1, 0 } },
		CanvasVertex{ _transform * area.bottomRight(), agbr, { 1, 1 } },
		CanvasVertex{ _transform * area.bottomLeft(), agbr, { 0, 1 } },
	});

	_geom.indices.insert(_geom.indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	getTopSurface().indexCount += 6;

	return *this;
}

Canvas& Canvas::texture(const TextureHandle& texture, const RectF& area, const TileArea& uvArea, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, texture);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * area.position, agbr, { uvArea.left, uvArea.top } },
		CanvasVertex{ _transform * area.topRight(), agbr, { uvArea.right, uvArea.top } },
		CanvasVertex{ _transform * area.bottomRight(), agbr, { uvArea.right, uvArea.bottom } },
		CanvasVertex{ _transform * area.bottomLeft(), agbr, { uvArea.left, uvArea.bottom } },
	});

	_geom.indices.insert(_geom.indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	getTopSurface().indexCount += 6;

	return *this;
}

Canvas& Canvas::strokeRect(const RectF& area, const Color4F& color) {
	std::array<PointF, 4> points = {
		area.position,
		area.topRight(),
		area.bottomRight(),
		area.bottomLeft()
	};

	return lines(points, color);
}

DimensionF Canvas::measureText(std::string_view text, std::string_view fontName, f32 fontSize) {
	FontHandle handle = loadFont(fontName, fontSize);
	FtglFont& font = handle.getResourceAs<FtglFont>();
	ftgl::texture_font_t* textureFont = font.getTextureFont();

	DimensionF ret(0, textureFont->height);

	for (size_t i = 0; i < text.size(); ++i) {
		ftgl::texture_glyph_t* glyph = ftgl::texture_font_get_glyph(textureFont, text.data() + i);

		if (i > 0) {
			ret.w += ftgl::texture_glyph_get_kerning(glyph, text.data() + i - 1);
		}

		ret.w += glyph->advance_x;
	}

	return ret;
}

Canvas& Canvas::text(const PointF& pos, std::string_view text, const Color4F& color) {
	assert(_font.isValid());

	FtglFont& font = _font.getResourceAs<FtglFont>();
	checkSurface(RenderPrimitive::Triangles, font.getTexture());

	ftgl::texture_font_t* textureFont = font.getTextureFont();

	uint32 agbr = toUint32Abgr(color);
	PointF vpos = _transform * pos;

	if (_textAlign & TextAlignFlags::Top) {
		vpos.y += textureFont->ascender;
	}

	for (size_t i = 0; i < text.size(); ++i) {
		ftgl::texture_glyph_t* glyph = ftgl::texture_font_get_glyph(textureFont, text.data() + i);

		if (glyph) {
			if (i > 0) {
				vpos.x += ftgl::texture_glyph_get_kerning(glyph, text.data() + i - 1);
			}

			f32 x0 = floor(vpos.x + glyph->offset_x);
			f32 y0 = floor(vpos.y - glyph->offset_y);
			f32 x1 = floor(x0 + glyph->width);
			f32 y1 = floor(y0 + glyph->height);
			f32 s0 = glyph->s0;
			f32 t0 = glyph->t0;
			f32 s1 = glyph->s1;
			f32 t1 = glyph->t1;

			uint32 v = (uint32)_geom.vertices.size();

			_geom.vertices.insert(_geom.vertices.end(), {
				CanvasVertex{ { x0, y0 }, agbr, { s0, t0 } },
				CanvasVertex{ { x1, y0 }, agbr, { s1, t0 } },
				CanvasVertex{ { x1, y1 }, agbr, { s1, t1 } },
				CanvasVertex{ { x0, y1 }, agbr, { s0, t1 } }
			});

			_geom.indices.insert(_geom.indices.end(), {
				v + 0, v + 3, v + 2,
				v + 2, v + 1, v + 0
			});

			getTopSurface().indexCount += 6;

			vpos.x += glyph->advance_x;
		}
	}

	return *this;
}
