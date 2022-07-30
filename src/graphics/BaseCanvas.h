#pragma once

#include "RpMath.h"

namespace rp::engine {
	/*struct CanvasTextureHandle {
		uint32 handle = 0;

		bool operator==(CanvasTextureHandle other) const {
			return handle == other.handle;
		}
	};*/

	class BaseCanvas {
	public:
		BaseCanvas() = default;
		virtual ~BaseCanvas() {}

		virtual void beginRender(Dimension res, f32 pixelRatio) = 0;

		virtual void endRender() = 0;

		virtual void translate(PointF amount) = 0;

		virtual void setScale(f32 scaleX, f32 scaleY) = 0;

		virtual void polygon(const PointF* points, uint32 count) = 0;

		inline void polygon(const std::vector<PointF>& points) {
			polygon(points.data(), (uint32)points.size());
		}

		template <const size_t PointCount>
		inline void polygon(const std::array<PointF, PointCount>& points) {
			polygon(points.data(), points.size());
		}

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
