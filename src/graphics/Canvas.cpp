#include "Canvas.h"

#include <fstream>

#include <spdlog/spdlog.h>

#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>

using namespace rp;
using namespace rp::engine;
namespace fs = std::filesystem;

const f32 PI = 3.14159265359f;
const f32 PI2 = PI * 2.0f;
constexpr PointF FIXED_VERTEX_OFFSET = PointF(0.00125f / 2.0f, 0);

uint32 toUint32Abgr(const Color4F& color) {
	return
		(static_cast<uint32>(color.a * 255.f) << 24) +
		(static_cast<uint32>(color.b * 255.f) << 16) +
		(static_cast<uint32>(color.g * 255.f) << 8) +
		(static_cast<uint32>(color.r * 255.f) << 0);
}

Canvas::Canvas(uint32 viewId): _viewId(viewId) {
	//_textureLookup.push_back(_defaultTexture);

	Dimension atlasSize = { 512, 512 };

	/*_atlas = ftgl::texture_atlas_new(atlasSize.w, atlasSize.h, 3);
	_font = ftgl::texture_font_new_from_file(_atlas, 12, "SEGOEUI.TTF");
	size_t missed = ftgl::texture_font_load_glyphs(_font, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+ø");
	//size_t missed = ftgl::texture_font_load_glyphs(_font, "ø");

	if (missed > 0) {
		spdlog::error("Missed {} glyphs", missed);
	}

	const uint32 size = atlasSize.w * atlasSize.h * 3;
	const bgfx::Memory* data = bgfx::alloc(size);
	memcpy(data->data, _atlas->data, size);
	auto atlasTex = bgfx::createTexture2D(atlasSize.w, atlasSize.h, false, 1, bgfx::TextureFormat::RGB8, 0, data);*/

	//_textureLookup.push_back(atlasTex);
}

Canvas::~Canvas() {
	//ftgl::texture_font_delete(_font);
	//ftgl::texture_atlas_delete(_atlas);
	
	//bgfx::destroy(_textureUniform);
	//bgfx::destroy(_defaultTexture);
	//bgfx::destroy(_prog);
	//bgfx::destroy(_vert);
	//bgfx::destroy(_ind);
}

entt::id_type getUriHash(std::string_view uri) {
	return entt::hashed_string(uri.data(), uri.size());
}

entt::id_type getPathUriHash(const std::filesystem::path& filePath) {
	std::string uri = filePath.relative_path().make_preferred().replace_extension().string();
	return getUriHash(uri);
}

/*TypedResourceHandle<TextureAtlas> Canvas::createTextureAtlas(std::string_view uri, const TypedResourceHandle<Texture>& texture, const std::unordered_map<entt::id_type, Rect>& tiles) {
	auto atlas = _textureAtlasCache.load(getUriHash(uri), texture, tiles);
	const DimensionF dim = (DimensionF)texture->dimensions;

	for (const auto& [uriHash, area] : tiles) {
		RectF textureUv(area.x / dim.w, area.y / dim.h, area.w / dim.w, area.h / dim.h);

		_tileLookup[uriHash] = {
			texture,
			Tile { .top = textureUv.y, .left = textureUv.x, .bottom = textureUv.bottom(), .right = textureUv.right() }
		};
	}

	return atlas.first->second;
}

TypedResourceHandle<Texture> Canvas::loadTexture(const std::filesystem::path& filePath) {
	entt::id_type uriHash = getPathUriHash(filePath);
	
	auto ret = _textureCache.load(uriHash, filePath);
	TypedResourceHandle<Texture> res = ret.first->second;

	if (res) {
		_tileLookup[uriHash] = { res, Tile { 0, 0, 1, 1 } };

		return res;
	}

	// Resource was not loaded!

	return res;
}*/

void Canvas::beginRender(Dimension res, f32 pixelRatio) {
	clear();

	_res.dimensions = res;
	_viewPort = Rect(0, 0, res.w, res.h);
	_pixelRatio = pixelRatio;

	_geom.surfaces.push_back(CanvasSurface{
		.primitive = RenderPrimitive::Triangles,
		.texture = _defaultTexture,
		.viewId = _viewId,
		.viewArea = _viewPort
	});
}

void Canvas::checkSurface(RenderPrimitive primitive, const TypedResourceHandle<Texture>& texture) {
	CanvasSurface& backSurf = _geom.surfaces.back();

	if (backSurf.indexCount == 0) {
		backSurf.primitive = primitive;
		backSurf.texture = texture;
	} else if (backSurf.primitive != primitive || backSurf.texture != texture) {
		_geom.surfaces.push_back(CanvasSurface{
			.primitive = primitive,
			.texture = texture,
			.indexOffset = backSurf.indexOffset + backSurf.indexCount,
			.viewId = _viewId,
			.viewArea = _viewPort
		});
	}
}

void Canvas::endRender() {

}

Canvas& Canvas::translate(PointF amount) {
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

	_geom.surfaces.back().indexCount += count;

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

	_geom.surfaces.back().indexCount += 6;

	return *this;
}

Canvas& Canvas::line(const PointF& from, const PointF& to, const Color4F& color) {
	checkSurface(RenderPrimitive::LineList, _defaultTexture);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * from, agbr, 0, 0 },
		CanvasVertex{ _transform * to, agbr, 0, 0 }
	});

	_geom.indices.insert(_geom.indices.end(), { v + 0, v + 1 });

	_geom.surfaces.back().indexCount += 2;

	return *this;
}

Canvas& Canvas::circle(const PointF& pos, f32 radius, uint32 segments, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, _defaultTexture);

	uint32 agbr = toUint32Abgr(color);

	f32 step = PI2 / (f32)segments;
	uint32 centerIdx = writeVertex(pos, agbr);

	for (uint32 i = 0; i < segments; ++i) {
		f32 rad = (f32)i * step;
		PointF p = PointF(cos(rad), sin(rad)) * radius;
		uint32 v = writeVertex(p + pos, agbr);

		if (i < segments - 1) {
			writeTriangleIndices(centerIdx, v, v + 1);
		} else {
			writeTriangleIndices(centerIdx, v, centerIdx + 1);
		}
	}

	return *this;
}

Canvas& Canvas::setScale(f32 scaleX, f32 scaleY) {
	return *this;
}

Canvas& Canvas::fillRect(const RectT<f32>& area, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, _defaultTexture);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * area.position, agbr, 0, 0 },
		CanvasVertex{ _transform * area.topRight(), agbr, 0, 0 },
		CanvasVertex{ _transform * area.bottomRight(), agbr, 0, 0 },
		CanvasVertex{ _transform * area.bottomLeft(), agbr, 0, 0 },
	});

	_geom.indices.insert(_geom.indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	_geom.surfaces.back().indexCount += 6;

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

	_geom.surfaces.back().indexCount += 6;

	return *this;
}

Canvas& Canvas::texture(entt::id_type uriHash, const RectT<f32>& area, const Color4F& color) {
	const auto foundTile = _tileLookup.find(uriHash);

	if (foundTile == _tileLookup.end()) {
		if (_invalidUris.find(uriHash) == _invalidUris.end()) {
			spdlog::error("Tried to render a texture that does not exist: {}", uriHash);
			_invalidUris.insert(uriHash);
		}

		return *this;
	}

	const auto texPair = foundTile->second;
	const auto& uv = texPair.second;

	checkSurface(RenderPrimitive::Triangles, texPair.first);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * area.position, agbr, { uv.left, uv.top } },
		CanvasVertex{ _transform * area.topRight(), agbr, { uv.right, uv.top } },
		CanvasVertex{ _transform * area.bottomRight(), agbr, { uv.right, uv.bottom } },
		CanvasVertex{ _transform * area.bottomLeft(), agbr, { uv.left, uv.bottom } },
	});

	_geom.indices.insert(_geom.indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	_geom.surfaces.back().indexCount += 6;

	return *this;
}

Canvas& Canvas::texture(const TypedResourceHandle<Texture>& texture, const RectT<f32>& area, const Color4F& color) {
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

	_geom.surfaces.back().indexCount += 6;

	return *this;
}

Canvas& Canvas::texture(const TypedResourceHandle<Texture>& texture, const Rect& textureArea, const RectT<f32>& area, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, texture);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_geom.vertices.size();

	auto dim = (DimensionF)texture.getResource().getDesc().dimensions;

	RectF textureUv(textureArea.x / dim.w, textureArea.y / dim.h, textureArea.w / dim.w, textureArea.h / dim.h);

	_geom.vertices.insert(_geom.vertices.end(), {
		CanvasVertex{ _transform * area.position, agbr, textureUv.position },
		CanvasVertex{ _transform * area.topRight(), agbr, textureUv.topRight() },
		CanvasVertex{ _transform * area.bottomRight(), agbr, textureUv.bottomRight() },
		CanvasVertex{ _transform * area.bottomLeft(), agbr, textureUv.bottomLeft() },
	});

	_geom.indices.insert(_geom.indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	_geom.surfaces.back().indexCount += 6;

	return *this;
}

Canvas& Canvas::strokeRect(const RectT<f32>& area, const Color4F& color) {
	return *this;
}

Canvas& Canvas::text(f32 x, f32 y, std::string_view text, const Color4F& color) {
	return *this;
	checkSurface(RenderPrimitive::Triangles, _defaultTexture);

	uint32 agbr = toUint32Abgr(color);

	for (size_t i = 0; i < text.size(); ++i) {
		ftgl::texture_glyph_t* glyph = ftgl::texture_font_get_glyph(_font, text.data() + i);

		if (glyph) {
			f32 kerning = 0.0f;

			if (i > 0) {
				kerning = texture_glyph_get_kerning(glyph, text.data() + i - 1);
			}

			x += kerning;

			f32 x0 = floor(x + glyph->offset_x);
			f32 y0 = floor(y - glyph->offset_y);
			f32 x1 = floor(x0 + glyph->width);
			f32 y1 = floor(y0 + glyph->height);
			f32 s0 = glyph->s0;
			f32 t0 = glyph->t0;
			f32 s1 = glyph->s1;
			f32 t1 = glyph->t1;

			uint32 v = (uint32)_geom.vertices.size();

			_geom.vertices.insert(_geom.vertices.end(), {
				CanvasVertex{ _transform * PointF{ x0, y0 }, agbr, s0,t0 },
				CanvasVertex{ _transform * PointF{ x1, y0 }, agbr, s1,t0 },
				CanvasVertex{ _transform * PointF{ x1, y1 }, agbr, s1,t1 },
				CanvasVertex{ _transform * PointF{ x0, y1 }, agbr, s0,t1 }
			});

			_geom.indices.insert(_geom.indices.end(), {
				v + 0, v + 3, v + 2,
				v + 2, v + 1, v + 0
			});

			_geom.surfaces.back().indexCount += 6;

			x += glyph->advance_x;
		}
	}

	return *this;
}
