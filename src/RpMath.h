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
	struct Point {
		T x = 0;
		T y = 0;

		bool operator==(Point other) const {
			return x == other.x && y == other.y;
		}

		bool operator!=(Point other) const {
			return x != other.x || y != other.y;
		}

		Point operator+(Point other) {
			return { x + other.x, y + other.y };
		}

		Point operator+(T value) {
			return { x + value, y + value };
		}

		Point& operator+=(Point other) {
			x += other.x;
			y += other.y;
			return *this;
		}

		Point& operator+=(T value) {
			x += value;
			y += value;
			return *this;
		}

		Point operator-(Point other) {
			return { x - other.x, y - other.y };
		}

		Point operator-(T value) {
			return { x - value, y - value };
		}

		Point& operator-=(Point other) {
			x -= other.x;
			y -= other.y;
			return *this;
		}

		Point& operator-=(T value) {
			x -= value;
			y -= value;
			return *this;
		}
	};

	template <typename T>
	struct Dimension {
		const static Dimension<T> zero;

		T w;
		T h;

		Dimension(): w(0), h(0) {}
		Dimension(const Dimension& other) : w(other.w), h(other.h) {}
		Dimension(T _w, T _h): w(_w), h(_h) {}

		bool operator==(const Dimension& other) const {
			return w == other.w && h == other.h;
		}

		bool operator!=(const Dimension& other) const {
			return w != other.w || h != other.h;
		}

		T area() const {
			return w * h;
		}
	};

	template <typename T>
	struct Rect {
		union {
			struct {
				T x;
				T y;
				T w;
				T h;
			};
			struct {
				Point<T> position;
				Dimension<T> dimensions;
			};
		};

		Rect(): x(0), y(0), w(0), h(0) {}
		Rect(T _x, T _y, T _w, T _h) : x(_x), y(_y), w(_w), h(_h) {}
		Rect(Point<T> pos, Dimension<T> dim) : position(pos), dimensions(dim) {}
		Rect(Dimension<T> dim) : position(0, 0), dimensions(dim) {}
		Rect(const Rect<T>& other) : x(other.x), y(other.y), w(other.w), h(other.h) {}

		bool operator==(const Rect<T>& other) const {
			return x == other.x && y == other.y && w == other.w && h == other.h;
		}

		bool operator!=(const Rect<T>& other) const {
			return x != other.x || y != other.y || w != other.w || h != other.h;
		}

		bool contains(Point<T> point) const {
			return point.x >= x && point.x < right() && point.y >= y && point.y < bottom();
		}

		bool intersects(const Rect<T>& other) const {
			return x < other.right() && right() > other.x && y > other.bottom() && bottom() < other.y;
		}

		Rect<T> shrink(T amount) const {
			return Rect<T>(x + amount, y + amount, w - amount * 2, h - amount * 2);
		}

		Rect<T> combine(const Rect<T>& other) const {
			Rect<T> ret = *this;

			if (other.x < ret.x) ret.x = other.x;
			if (other.y < ret.y) ret.y = other.y;
			if (other.right() > ret.right()) ret.setRight(other.right());
			if (other.bottom() > ret.bottom()) ret.setBottom(other.bottom());

			return ret;
		}

		Point<T> getCenter() const {
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

		Point<T> topRight() const {
			return { right(), y };
		}

		Point<T> bottomRight() const {
			return { right(), bottom() };
		}

		Point<T> bottomLeft() const {
			return { x, bottom() };
		}
	};
}
