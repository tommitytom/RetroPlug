#pragma once

#include "RpMath.h"

struct NVGcontext;
struct NVGcolor;

namespace rp {
	class NanovgCanvas {
	private:
		NVGcontext* _vg = nullptr;

	public:
		NanovgCanvas();
		~NanovgCanvas();

		void init();

		void beginRender(Dimension res, f32 pixelRatio);

		void endRender();

		void translate(PointF amount);

		void setScale(f32 scaleX, f32 scaleY);

		template <typename T>
		void fillRect(const DimensionT<T>& area, const Color4F& color) {
			fillRect(RectT<f32> { 0.0f, 0.0f, (f32)area.w, (f32)area.h }, color);
		}

		void fillRect(const Rect& area, const Color4F& color) {
			fillRect(static_cast<RectT<f32>>(area), color);
		}

		void fillRect(const RectT<f32>& area, const Color4F& color);

		void strokeRect(const Rect& area, const Color4F& color) {
			strokeRect(static_cast<RectT<f32>>(area), color);
		}

		void strokeRect(const RectT<f32>& area, const Color4F& color);

		void text(f32 x, f32 y, std::string_view text, const Color4F& color);
	};
}
