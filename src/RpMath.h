#pragma once

#include <assert.h>
#include "platform/Types.h"

namespace rp {
	struct Color3 {
		uint8 r = 0;
		uint8 g = 0;
		uint8 b = 0;
	};

	struct Color4 {
		uint8 r = 0;
		uint8 g = 0;
		uint8 b = 0;
		uint8 a = 0;

		Color4() {}
		Color4(uint8 _r, uint8 _g, uint8 _b, uint8 _a = 255): r(_r), g(_g), b(_b), a(_a) {}
		Color4(const Color3& other) : r(other.r), g(other.g), b(other.b), a(255) {}
		Color4(const Color4& other): r(other.r), g(other.g), b(other.b), a(other.a) {}

		bool operator==(Color4 color) const {
			return r == color.r && g == color.g && b == color.b && a == color.a;
		}
	};

	template <typename T>
	struct PointT {
		T x = 0;
		T y = 0;

		bool operator==(const PointT& other) const {
			return x == other.x && y == other.y;
		}

		bool operator!=(const PointT& other) const {
			return x != other.x || y != other.y;
		}

		PointT operator+(const PointT& other) {
			return { x + other.x, y + other.y };
		}

		PointT operator+(T value) {
			return { x + value, y + value };
		}

		PointT& operator+=(const PointT& other) {
			x += other.x;
			y += other.y;
			return *this;
		}

		PointT& operator+=(T value) {
			x += value;
			y += value;
			return *this;
		}

		PointT operator-(const PointT& other) {
			return { x - other.x, y - other.y };
		}

		PointT operator-(T value) {
			return { x - value, y - value };
		}

		PointT& operator-=(const PointT& other) {
			x -= other.x;
			y -= other.y;
			return *this;
		}

		PointT& operator-=(T value) {
			x -= value;
			y -= value;
			return *this;
		}

		template <typename R>
		explicit operator PointT<R>() const {
			return PointT<R> { (R)x, (R)y };
		}

		T magnitude() const { 
			if (std::is_integral_v<T>) {
				return (T)sqrt((x * x) + (y * y));
			} else {
				return (T)sqrtf((f32)((x * x) + (y * y)));
			}
		}

		T sqrMagnitude() const { 
			return (x * x) + (y * y); 
		}
	};
	using PointI32 = PointT<int32>;
	using PointU32 = PointT<uint32>;
	using PointF32 = PointT<f32>;
	using PointF64 = PointT<f64>;
	using Point = PointI32;
	using PointF = PointF32;

	template <typename T>
	struct DimensionT {
		const static DimensionT<T> zero;

		T w;
		T h;

		DimensionT(): w(0), h(0) {}
		DimensionT(const DimensionT& other) : w(other.w), h(other.h) {}
		DimensionT(T _w, T _h): w(_w), h(_h) {}

		bool operator==(const DimensionT& other) const {
			return w == other.w && h == other.h;
		}

		bool operator!=(const DimensionT& other) const {
			return w != other.w || h != other.h;
		}

		template <typename R>
		explicit operator DimensionT<R>() const {
			return DimensionT<R> { (R)w, (R)h };
		}

		T area() const {
			return w * h;
		}
	};
	using DimensionI32 = DimensionT<int32>;
	using DimensionU32 = DimensionT<uint32>;
	using DimensionF32 = DimensionT<f32>;
	using DimensionF64 = DimensionT<f64>;
	using Dimension = DimensionI32;
	using DimensionF = DimensionF32;

	template <typename T>
	struct RectT {
		union {
			struct {
				T x;
				T y;
				T w;
				T h;
			};
			struct {
				PointT<T> position;
				DimensionT<T> dimensions;
			};
		};

		RectT(): x(0), y(0), w(0), h(0) {}
		RectT(T _x, T _y, T _w, T _h) : x(_x), y(_y), w(_w), h(_h) {}
		RectT(PointT<T> pos, DimensionT<T> dim) : position(pos), dimensions(dim) {}
		RectT(DimensionT<T> dim) : position(0, 0), dimensions(dim) {}
		RectT(const RectT<T>& other) : x(other.x), y(other.y), w(other.w), h(other.h) {}

		bool operator==(const RectT<T>& other) const {
			return x == other.x && y == other.y && w == other.w && h == other.h;
		}

		bool operator!=(const RectT<T>& other) const {
			return x != other.x || y != other.y || w != other.w || h != other.h;
		}

		template <typename R>
		explicit operator RectT<R>() const {
			return RectT<R> { (R)x, (R)y, (R)w, (R)h };
		}

		bool contains(PointT<T> point) const {
			return point.x >= x && point.x < right() && point.y >= y && point.y < bottom();
		}

		bool intersects(const RectT<T>& other) const {
			return x < other.right() && right() > other.x && y > other.bottom() && bottom() < other.y;
		}

		RectT<T> shrink(T amount) const {
			return RectT<T>(x + amount, y + amount, w - amount * 2, h - amount * 2);
		}

		RectT<T> combine(const RectT<T>& other) const {
			RectT<T> ret = *this;

			if (other.x < ret.x) ret.x = other.x;
			if (other.y < ret.y) ret.y = other.y;
			if (other.right() > ret.right()) ret.setRight(other.right());
			if (other.bottom() > ret.bottom()) ret.setBottom(other.bottom());

			return ret;
		}

		PointT<T> getCenter() const {
			return { x + w / 2, y + h / 2 };
		}

		void setRight(T value) {
			assert(value >= x);
			w = value - x;
		}

		void setBottom(T value) {
			assert(value >= y);
			h = value - y;
		}

		T right() const {
			return x + w;
		}

		T bottom() const {
			return y + h;
		}

		T area() const {
			return w * h;
		}

		PointT<T> topRight() const {
			return { right(), y };
		}

		PointT<T> bottomRight() const {
			return { right(), bottom() };
		}

		PointT<T> bottomLeft() const {
			return { x, bottom() };
		}
	};
	using RectI32 = RectT<int32>;
	using RectU32 = RectT<uint32>;
	using RectF32 = RectT<f32>;
	using RectF64 = RectT<f64>;
	using Rect = RectI32;
	using RectF = RectF32;
}
