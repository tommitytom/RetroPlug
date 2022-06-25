#pragma once

#include <vector>

#include <bgfx/bgfx.h>

#include "RpMath.h"
#include "BaseCanvas.h"

namespace rp::engine {
	struct CanvasVertex {
		PointF pos;
		uint32_t abgr;
	};

	class BgfxCanvas : public BaseCanvas {
	private:
		std::vector<CanvasVertex> _vertices;
		std::vector<uint32> _indices;

		Dimension _res;
		f32 _pixelRatio = 1.0f;

		bgfx::DynamicVertexBufferHandle _vert;
		bgfx::DynamicIndexBufferHandle _ind;
		bgfx::ProgramHandle _prog;

	public:
		BgfxCanvas();
		~BgfxCanvas();

		void beginRender(Dimension res, f32 pixelRatio) override;

		void endRender() override;

		void translate(PointF amount) override;

		void setScale(f32 scaleX, f32 scaleY) override;

		void fillRect(const RectT<f32>& area, const Color4F& color) override;

		void strokeRect(const RectT<f32>& area, const Color4F& color) override;

		void text(f32 x, f32 y, std::string_view text, const Color4F& color) override;
	};
}
