#pragma once

#include <cmath>
#include <assert.h>
#include "foundation/Types.h"

namespace fw {
	constexpr f32 PI = 3.14159265358979323846264338327950288F;
	constexpr f32 PI2 = PI * 2.0f;
	constexpr f32 HALF_PI = PI * 0.5f;
	constexpr f32 QUARTER_PI = PI * 0.25f;

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

		Color4() = default;
		Color4(uint8 _r, uint8 _g, uint8 _b, uint8 _a = 255): r(_r), g(_g), b(_b), a(_a) {}
		Color4(const Color3& other) : r(other.r), g(other.g), b(other.b), a(255) {}
		Color4(const Color4& other): r(other.r), g(other.g), b(other.b), a(other.a) {}

		bool operator==(Color4 color) const {
			return r == color.r && g == color.g && b == color.b && a == color.a;
		}

		static const Color4 clear;
		static const Color4 white;
		static const Color4 black;
		static const Color4 red;
		static const Color4 blue;
		static const Color4 green;
		static const Color4 lightGrey;
		static const Color4 darkGrey;
	};

	inline const Color4 Color4::clear = Color4(0, 0, 0, 0);
	inline const Color4 Color4::white = Color4(255, 255, 255, 255);
	inline const Color4 Color4::black = Color4(0, 0, 0, 255);
	inline const Color4 Color4::red = Color4(255, 0, 0, 255);
	inline const Color4 Color4::blue = Color4(0, 0, 255, 255);
	inline const Color4 Color4::green = Color4(0, 255, 0, 255);
	inline const Color4 Color4::lightGrey = Color4(170, 170, 170, 255);
	inline const Color4 Color4::darkGrey = Color4(50, 50, 50, 255);

	struct Color4F {
		f32 r = 0;
		f32 g = 0;
		f32 b = 0;
		f32 a = 0;

		Color4F() = default;
		Color4F(f32 _r, f32 _g, f32 _b, f32 _a = 1) : r(_r), g(_g), b(_b), a(_a) {}
		Color4F(const Color3& other) : r(other.r / 255.0f), g(other.g / 255.0f), b(other.b / 255.0f), a(1) {}
		Color4F(const Color4& other) : r(other.r / 255.0f), g(other.g / 255.0f), b(other.b / 255.0f), a(other.a / 255.0f) {}

		bool operator==(Color4F color) const {
			return r == color.r && g == color.g && b == color.b && a == color.a;
		}

		static const Color4F clear;
		static const Color4F white;
		static const Color4F black;
		static const Color4F red;
		static const Color4F blue;
		static const Color4F green;
		static const Color4F lightGrey;
		static const Color4F darkGrey;
	};

	inline const Color4F Color4F::clear = Color4F(0.0f, 0.0f, 0.0f, 0.0f);
	inline const Color4F Color4F::white = Color4F(1.0f, 1.0f, 1.0f, 1.0f);
	inline const Color4F Color4F::black = Color4F(0.0f, 0.0f, 0.0f, 1.0f);
	inline const Color4F Color4F::red = Color4F(1.0f, 0.0f, 0.0f, 1.0f);
	inline const Color4F Color4F::blue = Color4F(0.0f, 0.0f, 1.0f, 1.0f);
	inline const Color4F Color4F::green = Color4F(0.0f, 1.0f, 0.0f, 1.0f);
	inline const Color4F Color4F::lightGrey = Color4F(0.66f, 0.66f, 0.66f, 1.0f);
	inline const Color4F Color4F::darkGrey = Color4F(0.2f, 0.2f, 0.2f, 1.0f);

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

		PointT operator+() const {
			return { +x, +y };
		}

		PointT operator+(const PointT& other) const {
			return { x + other.x, y + other.y };
		}

		PointT operator+(T value) const {
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


		PointT operator-() const {
			return { -x, -y };
		}

		PointT operator-(const PointT& other) const {
			return { x - other.x, y - other.y };
		}

		PointT operator-(T value) const {
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


		PointT operator*(const PointT& other) const {
			return { x * other.x, y * other.y };
		}

		PointT operator*(T value) const {
			return { x * value, y * value };
		}

		PointT& operator*=(const PointT& other) {
			x *= other.x;
			y *= other.y;
			return *this;
		}

		PointT& operator*=(T value) {
			x *= value;
			y *= value;
			return *this;
		}


		PointT operator/(const PointT& other) const {
			return { x / other.x, y / other.y };
		}

		PointT operator/(T value) const {
			return { x / value, y / value };
		}

		PointT& operator/=(const PointT& other) {
			x /= other.x;
			y /= other.y;
			return *this;
		}

		PointT& operator/=(T value) {
			x /= value;
			y /= value;
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


		DimensionT operator+() const {
			return { +w, +h };
		}

		DimensionT operator+(const DimensionT& other) const {
			return { w + other.w, h + other.h };
		}

		DimensionT operator+(T value) const {
			return { w + value, h + value };
		}

		DimensionT& operator+=(const DimensionT& other) {
			w += other.w;
			h += other.h;
			return *this;
		}

		DimensionT& operator+=(T value) {
			w += value;
			h += value;
			return *this;
		}


		DimensionT operator-() const {
			return { -w, -h };
		}

		DimensionT operator-(const DimensionT& other) const {
			return { w - other.w, h - other.h };
		}

		DimensionT operator-(T value) const {
			return { w - value, h - value };
		}

		DimensionT& operator-=(const DimensionT& other) {
			w -= other.w;
			h -= other.h;
			return *this;
		}

		DimensionT& operator-=(T value) {
			w -= value;
			h -= value;
			return *this;
		}


		DimensionT operator*(const DimensionT& other) const {
			return { w * other.w, h * other.h };
		}

		DimensionT operator*(T value) const {
			return { w * value, h * value };
		}

		DimensionT& operator*=(const DimensionT& other) {
			w *= other.w;
			h *= other.h;
			return *this;
		}

		DimensionT& operator*=(T value) {
			w *= value;
			h *= value;
			return *this;
		}


		DimensionT operator/(const DimensionT& other) const {
			return { w / other.w, h / other.h };
		}

		DimensionT operator/(T value) const {
			return { w / value, h / value };
		}

		DimensionT& operator/=(const DimensionT& other) {
			w /= other.w;
			h /= other.h;
			return *this;
		}

		DimensionT& operator/=(T value) {
			w /= value;
			h /= value;
			return *this;
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

		/*bool contains(const Rect<T>& other) const {
			return other.x > x && other.y < y && other.right() < right() && other.bottom() < bottom();
		}*/

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

	struct Mat3x3 {
		static Mat3x3 translation(const PointF& position) {
			return Mat3x3(
				1, 0, position.x,
				0, 1, position.y,
				0, 0, 1
			);
		}

		static Mat3x3 scale(const PointF& size) {
			return Mat3x3(
				size.x, 0, 0,
				0, size.y, 0,
				0, 0, 1
			);
		}

		static Mat3x3 rotation(f32 angle) {
			return Mat3x3(
				cosf(angle), -sinf(angle), 0,
				sinf(angle), cosf(angle), 0,
				0, 0, 1
			);
		}

		static Mat3x3 trs(const PointF& translation, f32 rotation, const PointF& scale) {
			return Mat3x3::scale(scale) * Mat3x3::rotation(rotation) * Mat3x3::translation(translation);
		}

		union {
			f32 m[3][3];
			f32 ma[9];
			struct {
				f32 m11;
				f32 m12;
				f32 m13;
				f32 m21;
				f32 m22;
				f32 m23;
				f32 m31;
				f32 m32;
				f32 m33;
			};
		};

		constexpr Mat3x3(f32 v = 1.0f) : Mat3x3(v, 0.0f, 0.0f, 0.0f, v, 0.0f, 0.0f, 0.0f, v) {}

		constexpr Mat3x3(f32 _m11, f32 _m12, f32 _m13, f32 _m21, f32 _m22, f32 _m23, f32 _m31, f32 _m32, f32 _m33)
			: m11(_m11), m12(_m12), m13(_m13), m21(_m21), m22(_m22), m23(_m23), m31(_m31), m32(_m32), m33(_m33) { }

		Mat3x3 operator*(const Mat3x3& other) const {
			Mat3x3 result;

			result.m11 = m11 * other.m11 + m21 * other.m12 + m31 * other.m13;
			result.m12 = m12 * other.m11 + m22 * other.m12 + m32 * other.m13;
			result.m13 = m13 * other.m11 + m23 * other.m12 + m33 * other.m13;

			result.m21 = m11 * other.m21 + m21 * other.m22 + m31 * other.m23;
			result.m22 = m12 * other.m21 + m22 * other.m22 + m32 * other.m23;
			result.m23 = m13 * other.m21 + m23 * other.m22 + m33 * other.m23;

			result.m31 = m11 * other.m31 + m21 * other.m32 + m31 * other.m33;
			result.m32 = m12 * other.m31 + m22 * other.m32 + m32 * other.m33;
			result.m33 = m13 * other.m31 + m23 * other.m32 + m33 * other.m33;

			return result;
		}

		PointF operator*(const PointF& other) const {
			return PointF{
				.x = (m11 * other.x) + (m12 * other.y) + m13,
				.y = (m21 * other.x) + (m22 * other.y) + m23
			};
		}

		PointF getTranslation() const {
			return PointF{ m13, m23 };
		}

		PointF getScale() const {
			return PointF{ m11, m22 };
		}
	};
}
