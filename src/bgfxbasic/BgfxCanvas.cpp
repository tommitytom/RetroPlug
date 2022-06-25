#include "BgfxCanvas.h"

#include "shaders/fs_tex.h"
#include "shaders/vs_tex.h"

using namespace rp;
using namespace rp::engine;

static CanvasVertex quadVertices[] = {
	{-0.5f,  0.5f,  0xff000000},
	{ 0.5f,  0.5f,  0xff0000ff},
	{ 0.5f, -0.5f,  0xff00ff00},
	{-0.5f, -0.5f,  0xff00ffff},
};

static const uint32 quadTriList[] = {
	0, 3, 2,
	2, 1, 0
};

static bgfx::ShaderHandle loadShader(const uint8_t* data, size_t size, const char* name = nullptr) {
	const bgfx::Memory* mem = bgfx::makeRef(data, size);
	bgfx::ShaderHandle handle = bgfx::createShader(mem);

	if (name) {
		bgfx::setName(handle, name);
	}

	return handle;
}

BgfxCanvas::BgfxCanvas() {
	bgfx::VertexLayout uivDecl;
	uivDecl.begin()
		.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.end();

	_vert = bgfx::createDynamicVertexBuffer(bgfx::makeRef(quadVertices, sizeof(quadVertices)), uivDecl);
	_ind = bgfx::createDynamicIndexBuffer(bgfx::makeRef(quadTriList, sizeof(quadTriList)), BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE);

	bgfx::ShaderHandle vsh = loadShader(vs_tex, sizeof(vs_tex), "Canvas Vertex Shader");
	bgfx::ShaderHandle fsh = loadShader(fs_tex, sizeof(fs_tex), "Canvas Pixel Shader");

	_prog = bgfx::createProgram(vsh, fsh, true);
}

BgfxCanvas::~BgfxCanvas() {
	bgfx::destroy(_prog);
	bgfx::destroy(_vert);
	bgfx::destroy(_ind);
}

void BgfxCanvas::beginRender(Dimension res, f32 pixelRatio) {
	_res = res;
	_pixelRatio = pixelRatio;
}

void BgfxCanvas::endRender() {
	if (_vertices.size()) {
		const bgfx::Memory* verts = bgfx::copy(_vertices.data(), _vertices.size() * sizeof(CanvasVertex));
		const bgfx::Memory* inds = bgfx::copy(_indices.data(), _indices.size() * sizeof(uint32));
		bgfx::update(_vert, 0, verts);
		bgfx::update(_ind, 0, inds);

		bgfx::setVertexBuffer(0, _vert);
		bgfx::setIndexBuffer(_ind);
		bgfx::submit(0, _prog);

		_vertices.clear();
		_indices.clear();
	}
}

void BgfxCanvas::translate(PointF amount) {

}

void BgfxCanvas::setScale(f32 scaleX, f32 scaleY) {

}

void BgfxCanvas::fillRect(const RectT<f32>& area, const Color4F& color) {
	uint32 v = (uint32)_vertices.size();

	RectT<f32> areaFlipped = area;
	areaFlipped.h = -areaFlipped.h;

	_vertices.insert(_vertices.end(), {
		CanvasVertex{ areaFlipped.position, 0xFFFFFFFF },
		CanvasVertex{ areaFlipped.topRight(), 0xFFFFFFFF},
		CanvasVertex{ areaFlipped.bottomRight(), 0xFFFFFFFF},
		CanvasVertex{ areaFlipped.bottomLeft(), 0xFFFFFFFF},
	});

	_indices.insert(_indices.end(), {
		v + 0, v + 3, v + 2,
		v + 2, v + 1, v + 0
	});
}

void BgfxCanvas::strokeRect(const RectT<f32>& area, const Color4F& color) {

}

void BgfxCanvas::text(f32 x, f32 y, std::string_view text, const Color4F& color) {

}
