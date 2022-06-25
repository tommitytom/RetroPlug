#pragma once

#include "RpMath.h"

namespace rp::engine {
	class BaseCanvas {
	public:
		BaseCanvas() = default;
		virtual ~BaseCanvas() {}

		virtual void beginRender(Dimension res, f32 pixelRatio) = 0;

		virtual void endRender() = 0;

		virtual void translate(PointF amount) = 0;

		virtual void setScale(f32 scaleX, f32 scaleY) = 0;

		template <typename T>
		void fillRect(const DimensionT<T>& area, const Color4F& color) {
			fillRect(RectT<f32> { 0.0f, 0.0f, (f32)area.w, (f32)area.h }, color);
		}

		void fillRect(const Rect& area, const Color4F& color) {
			fillRect(static_cast<RectT<f32>>(area), color);
		}

		virtual void fillRect(const RectT<f32>& area, const Color4F& color) = 0;

		void strokeRect(const Rect& area, const Color4F& color) {
			strokeRect(static_cast<RectT<f32>>(area), color);
		}

		virtual void strokeRect(const RectT<f32>& area, const Color4F& color) = 0;

		virtual void text(f32 x, f32 y, std::string_view text, const Color4F& color) = 0;
	};
}
