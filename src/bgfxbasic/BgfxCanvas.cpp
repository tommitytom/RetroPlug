#include "BgfxCanvas.h"

#include <fstream>

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/error.h>

#include <spdlog/spdlog.h>

#include "shaders/fs_tex.h"
#include "shaders/vs_tex.h"

using namespace rp;
using namespace rp::engine;
namespace fs = std::filesystem;

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
}

BgfxCanvas::~BgfxCanvas() {
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

		bgfx::setVertexBuffer(0, _vert);

		f32 scale[4] = { 2.0f / _res.w, 2.0f / _res.h, 0.0f, 0.0f };

		for (const CanvasSurface& surface : _surfaces) {
			bgfx::setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
				//| BGFX_STATE_WRITE_Z
				//| BGFX_STATE_DEPTH_TEST_LESS
				//| BGFX_STATE_CULL_CW
				//| BGFX_STATE_MSAA
			);

			bgfx::setUniform(_scaleUniform, scale);
			bgfx::setTexture(0, _textureUniform, _textureLookup[surface.texture.handle]);
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

	uint32 v = (uint32)_vertices.size();
	uint32 agbr = toUint32Abgr(Color4F(1, 1, 1, 1));

	for (uint32 i = 0; i < count; ++i) {
		_vertices.push_back(CanvasVertex{ _transform * points[i], agbr, 0, 0 });
	}

	_indices.insert(_indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});

	_surfaces.back().indexCount += 6;
}

const f32 PI = 3.14159265359f;
const f32 PI2 = PI * 2.0f;

void BgfxCanvas::circle(const PointF& pos, f32 radius, uint32 segments) {
	checkSurface(RenderPrimitive::Triangles, CanvasTextureHandle());

	f32 step = PI2 / (f32)segments;
	uint32 centerIdx = writeVertex(pos, Color4F(1, 1, 1, 1));

	for (uint32 i = 0; i < segments; ++i) {
		f32 rad = (f32)i * step;
		PointF p = PointF(cos(rad), sin(rad)) * radius;
		uint32 v = writeVertex(p + pos, Color4F(1, 1, 1, 1));

		if (i < segments - 1) {
			writeTriangleIndices(centerIdx, v, v + 1);
		} else {
			writeTriangleIndices(centerIdx, v, centerIdx + 1);
		}
	}
}

uint32 BgfxCanvas::writeVertex(PointF pos, const Color4F& color) {
	_vertices.push_back(CanvasVertex { _transform * pos, toUint32Abgr(color), 0, 0 });
	return (uint32)_vertices.size() - 1;
}

void BgfxCanvas::setScale(f32 scaleX, f32 scaleY) {

}

void BgfxCanvas::fillRect(const RectT<f32>& area, const Color4F& color) {
	checkSurface(RenderPrimitive::Triangles, CanvasTextureHandle());

	uint32 v = (uint32)_vertices.size();

	uint32 agbr = toUint32Abgr(color);

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

	uint32 v = (uint32)_vertices.size();

	uint32 agbr = toUint32Abgr(color);

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

}
