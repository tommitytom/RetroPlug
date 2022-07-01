#include "BgfxCanvas.h"

#include <fstream>

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/error.h>

#include <spdlog/spdlog.h>

#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>

#include "shaders/fs_tex.h"
#include "shaders/vs_tex.h"

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

bgfx::ShaderHandle loadShader(const uint8_t* data, size_t size, const char* name = nullptr) {
	const bgfx::Memory* mem = bgfx::makeRef(data, size);
	bgfx::ShaderHandle handle = bgfx::createShader(mem);

	if (name) {
		bgfx::setName(handle, name);
	}

	return handle;
}

std::vector<std::byte> readFile(const fs::path& path) {
	std::vector<std::byte> target;
	std::ifstream f(path.lexically_normal(), std::ios::binary);

	//es_assert(f.is_open());
	if (!f.is_open()) {
		return target;
	}

	f.seekg(0, std::ios::end);
	std::streamoff size = f.tellg();
	f.seekg(0, std::ios::beg);

	target.resize((size_t)size);
	f.read((char*)target.data(), target.size());

	return target;
}

bgfx::TextureHandle createWhiteTexture(uint32 w, uint32 h) {
	const size_t size = w * h * 4;
	const bgfx::Memory* data = bgfx::alloc(size);
	memset(data->data, 0xFF, size);
	return bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, 0, data);
}

BgfxCanvas::BgfxCanvas(bgfx::ViewId viewId): _viewId(viewId) {
	bgfx::ShaderHandle vsh = loadShader(vs_tex, sizeof(vs_tex), "Canvas Vertex Shader");
	bgfx::ShaderHandle fsh = loadShader(fs_tex, sizeof(fs_tex), "Canvas Pixel Shader");

	_prog = bgfx::createProgram(vsh, fsh, true);

	_textureUniform = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
	_whiteTexture = createWhiteTexture(8, 8);

	_scaleUniform = bgfx::createUniform("scale", bgfx::UniformType::Vec4);

	_textureLookup.push_back(_whiteTexture);

	Dimension atlasSize = { 512, 512 };

	_atlas = ftgl::texture_atlas_new(atlasSize.w, atlasSize.h, 3);
	_font = ftgl::texture_font_new_from_file(_atlas, 32, "Roboto-Regular.ttf");
	size_t missed = ftgl::texture_font_load_glyphs(_font, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+");

	if (missed > 0) {
		spdlog::error("Missed {} glyphs", missed);
	}

	const size_t size = atlasSize.w * atlasSize.h * 3;
	const bgfx::Memory* data = bgfx::alloc(size);
	memcpy(data->data, _atlas->data, size);
	auto atlasTex = bgfx::createTexture2D(atlasSize.w, atlasSize.h, false, 1, bgfx::TextureFormat::RGB8, 0, data);

	_textureLookup.push_back(atlasTex);
}

BgfxCanvas::~BgfxCanvas() {
	ftgl::texture_font_delete(_font);
	ftgl::texture_atlas_delete(_atlas);
	
	bgfx::destroy(_textureUniform);
	bgfx::destroy(_whiteTexture);
	bgfx::destroy(_prog);
	bgfx::destroy(_vert);
	bgfx::destroy(_ind);
}

CanvasTextureHandle BgfxCanvas::loadTexture(const std::filesystem::path& filePath) {
	if (fs::exists(filePath)) {
		uintmax_t fileSize = fs::file_size(filePath);
		
		std::vector<std::byte> fileData = readFile(filePath);
		if (fileData.size() > 0) {
			bx::Error err;

			bimg::ImageContainer* imageContainer = bimg::imageParse(
				(bx::AllocatorI*)&_alloc,
				(const void*)fileData.data(),
				(uint32_t)fileData.size()
			);

			if (imageContainer) {
				const bgfx::Memory* mem = bgfx::copy(imageContainer->m_data, imageContainer->m_size);
				
				bgfx::TextureHandle handle = bgfx::createTexture2D(
					uint16_t(imageContainer->m_width),
					uint16_t(imageContainer->m_height),
					imageContainer->m_numMips > 1,
					imageContainer->m_numLayers,
					bgfx::TextureFormat::Enum(imageContainer->m_format),
					0,
					mem
				);

				_textureLookup.push_back(handle);

				return CanvasTextureHandle((uint32)_textureLookup.size() - 1);
			} else {
				spdlog::error("Failed to load texture at {}: {}", filePath.string(), err.getMessage().getPtr());
			}
		}
	}
	
	return CanvasTextureHandle();
}

void BgfxCanvas::beginRender(Dimension res, f32 pixelRatio) {
	_res = res;
	_viewPort = Rect(0, 0, res.w, res.h);
	_pixelRatio = pixelRatio;

	_surfaces.push_back(CanvasSurface {
		.primitive = RenderPrimitive::Triangles,
		.texture = CanvasTextureHandle(),
	});
}

void BgfxCanvas::checkSurface(RenderPrimitive primitive, CanvasTextureHandle texture) {
	CanvasSurface& backSurf = _surfaces.back();

	if (backSurf.indexCount == 0) {
		backSurf.primitive = primitive;
		backSurf.texture = texture;
	} else if (backSurf.primitive != primitive || backSurf.texture != texture) {
		_surfaces.push_back(CanvasSurface {
			.primitive = primitive,
			.texture = texture,
			.indexOffset = backSurf.indexOffset + backSurf.indexCount
		});
	}
}

void BgfxCanvas::endRender() {
	if (_vertices.size()) {
		const bgfx::Memory* verts = bgfx::copy(_vertices.data(), _vertices.size() * sizeof(CanvasVertex));
		const bgfx::Memory* inds = bgfx::copy(_indices.data(), _indices.size() * sizeof(uint32));

		if (bgfx::isValid(_vert)) {
			bgfx::update(_vert, 0, verts);
			bgfx::update(_ind, 0, inds); 
		} else {
			bgfx::VertexLayout uivDecl;
			uivDecl.begin()
				.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
				.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
				.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, true)
				.end();

			_vert = bgfx::createDynamicVertexBuffer(verts, uivDecl);
			_ind = bgfx::createDynamicIndexBuffer(inds, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE);
		}

		f32 scale[4] = { 2.0f / _res.w, 2.0f / _res.h, 0.0f, 0.0f };

		for (const CanvasSurface& surface : _surfaces) {
			uint32 state = 0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
				//| BGFX_STATE_WRITE_Z
				//| BGFX_STATE_DEPTH_TEST_LESS
				//| BGFX_STATE_CULL_CW
				//| BGFX_STATE_MSAA
				;

			switch (surface.primitive) {
			case RenderPrimitive::LineList:
				state |= BGFX_STATE_PT_LINES | BGFX_STATE_LINEAA;
				break;
			case RenderPrimitive::LineStrip:
				state |= BGFX_STATE_PT_LINESTRIP | BGFX_STATE_LINEAA;
				break;
			case RenderPrimitive::Points:
				state |= BGFX_STATE_PT_POINTS;
				break;
			case RenderPrimitive::TriangleStrip:
				state |= BGFX_STATE_PT_TRISTRIP;
				break;
			}

			bgfx::setState(state);

			bgfx::setUniform(_scaleUniform, scale);
			bgfx::setTexture(0, _textureUniform, _textureLookup[surface.texture.handle]);
			bgfx::setVertexBuffer(0, _vert);
			bgfx::setIndexBuffer(_ind, surface.indexOffset, surface.indexCount);
			bgfx::submit(_viewId, _prog);
		}

		_vertices.clear();
		_indices.clear();
		_surfaces.clear();
	}
}

void BgfxCanvas::translate(PointF amount) {

}

void BgfxCanvas::polygon(const PointF* points, uint32 count) {
	checkSurface(RenderPrimitive::Triangles, CanvasTextureHandle());

	uint32 agbr = toUint32Abgr(Color4F(1, 1, 1, 1));
	uint32 v = (uint32)_vertices.size();

	for (uint32 i = 0; i < count; ++i) {
		_vertices.push_back(CanvasVertex{ _transform * points[i], agbr, 0, 0 });
	}

	_indices.insert(_indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	_surfaces.back().indexCount += 6;
}

void BgfxCanvas::line(const PointF& from, const PointF& to, const Color4F& color) {
	checkSurface(RenderPrimitive::LineList, CanvasTextureHandle());

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_vertices.size();

	_vertices.insert(_vertices.end(), {
		CanvasVertex{ _transform * from, agbr, 0, 0 },
		CanvasVertex{ _transform * to, agbr, 0, 0 }
	});

	_indices.insert(_indices.end(), { v + 0, v + 1 });

	_surfaces.back().indexCount += 2;
}

void BgfxCanvas::circle(const PointF& pos, f32 radius, uint32 segments, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, CanvasTextureHandle());

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
}

void BgfxCanvas::setScale(f32 scaleX, f32 scaleY) {

}

void BgfxCanvas::fillRect(const RectT<f32>& area, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, CanvasTextureHandle());

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_vertices.size();

	_vertices.insert(_vertices.end(), {
		CanvasVertex{ _transform * area.position, agbr, 0, 0 },
		CanvasVertex{ _transform * area.topRight(), agbr, 0, 0 },
		CanvasVertex{ _transform * area.bottomRight(), agbr, 0, 0 },
		CanvasVertex{ _transform * area.bottomLeft(), agbr, 0, 0 },
	});

	_indices.insert(_indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	_surfaces.back().indexCount += 6;
}

void BgfxCanvas::texture(CanvasTextureHandle textureHandle, const RectT<f32>& area, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, textureHandle);

	uint32 agbr = toUint32Abgr(color);
	uint32 v = (uint32)_vertices.size();

	_vertices.insert(_vertices.end(), {
		CanvasVertex{ area.position, agbr, 0, 0 },
		CanvasVertex{ area.topRight(), agbr, 1, 0 },
		CanvasVertex{ area.bottomRight(), agbr, 1, 1 },
		CanvasVertex{ area.bottomLeft(), agbr, 0, 1 },
	});

	_indices.insert(_indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	_surfaces.back().indexCount += 6;
}

void BgfxCanvas::strokeRect(const RectT<f32>& area, const Color4F& color) {

}

void BgfxCanvas::text(f32 x, f32 y, std::string_view text, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, CanvasTextureHandle(1));

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

			uint32 v = (uint32)_vertices.size();

			_vertices.insert(_vertices.end(), {
				CanvasVertex{ { x0, y0 }, agbr, s0,t0 },
				CanvasVertex{ { x1, y0 }, agbr, s1,t0 },
				CanvasVertex{ { x1, y1 }, agbr, s1,t1 },
				CanvasVertex{ { x0, y1 }, agbr, s0,t1 }
			});

			_indices.insert(_indices.end(), {
				v + 0, v + 3, v + 2,
				v + 2, v + 1, v + 0
			});

			_surfaces.back().indexCount += 6;

			x += glyph->advance_x;
		}
	}
}
