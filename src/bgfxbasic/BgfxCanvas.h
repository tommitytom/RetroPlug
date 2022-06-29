#pragma once

#include <filesystem>
#include <vector>

#include <bgfx/bgfx.h>
#include <bx/allocator.h>

#include "RpMath.h"
#include "BaseCanvas.h"

namespace rp::engine {
	struct CanvasVertex {
		PointF pos;
		uint32_t abgr;
		f32 u;
		f32 v;
	};

	enum class RenderPrimitive {
		Points = 0x0000,
		LineList = 0x0001,
		LineLoop = 0x0002,
		LineStrip = 0x0003,
		Triangles = 0x0004,
		TriangleStrip = 0x0005,
		TriangleFan = 0x0006,
	};

	struct CanvasSurface {
		RenderPrimitive primitive = RenderPrimitive::Triangles;
		CanvasTextureHandle texture;
		size_t indexOffset = 0;
		size_t indexCount = 0;
	};

	class BgfxCanvas : public BaseCanvas {
	private:
		std::vector<CanvasVertex> _vertices;
		std::vector<uint32> _indices;
		std::vector<CanvasSurface> _surfaces;

		Rect _viewPort;
		Rect _res;
		f32 _pixelRatio = 1.0f;

		bgfx::DynamicVertexBufferHandle _vert = BGFX_INVALID_HANDLE;
		bgfx::DynamicIndexBufferHandle _ind = BGFX_INVALID_HANDLE;
		bgfx::ProgramHandle _prog;

		bgfx::TextureHandle _whiteTexture;
		bgfx::UniformHandle _textureUniform;
		bgfx::UniformHandle _scaleUniform;

		bgfx::ViewId _viewId;

		std::vector<bgfx::TextureHandle> _textureLookup;

		bx::DefaultAllocator _alloc;

		PointF _offset = { 0, 0 };
		PointF _scale = { 1, 1 };
		f32 _rotation = 0.0f;
		Mat3x3 _transform;

	public:
		BgfxCanvas(bgfx::ViewId viewId = 0);
		~BgfxCanvas();

		void setViewId(bgfx::ViewId viewId) {
			_viewId = viewId;
		}

		void setTransform(PointF position, PointF scale = { 1, 1 }, f32 rotation = 0.0f) {
			_offset = position;
			_scale = scale;
			_rotation = rotation;
			_transform = Mat3x3::translation(position);
		}

		void setTransform(const Mat3x3& transform) {
			_transform = transform;
		}

		void clearTransform() {
			_offset = { 0, 0 };
			_scale = { 1, 1 };
			_rotation = 0.0f;
			_transform = Mat3x3();
		}

		CanvasTextureHandle loadTexture(const std::filesystem::path& filePath);

		void beginRender(Dimension res, f32 pixelRatio) override;

		void endRender() override;

		void translate(PointF amount) override;

		void polygon(const PointF* points, uint32 count);

		void setScale(f32 scaleX, f32 scaleY) override;

		void fillRect(const RectT<f32>& area, const Color4F& color) override;

		void texture(CanvasTextureHandle textureHandle, const RectT<f32>& area, const Color4F& color);

		void strokeRect(const RectT<f32>& area, const Color4F& color) override;

		void text(f32 x, f32 y, std::string_view text, const Color4F& color) override;

		void circle(const PointF& pos, f32 radius, uint32 segments = 32);

	private:
		void checkSurface(RenderPrimitive primitive, CanvasTextureHandle texture);

		uint32 writeVertex(PointF pos, const Color4F& color);

		void writeTriangleIndices(uint32 v1, uint32 v2, uint32 v3) {
			_indices.insert(_indices.end(), { v1, v2, v3 });
			_surfaces.back().indexCount += 3;
		}
	};
}
