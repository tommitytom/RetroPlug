#pragma once

#include "foundation/Types.h"
#include "foundation/DataBuffer.h"

namespace fw {
	class Image {
	private:
		Color4Buffer _buffer;
		Dimension _dimensions;

	public:
		Image() {}
		Image(Dimension dimensions) : _buffer((size_t)(dimensions.area())), _dimensions(dimensions) {}
		Image(int32 w, int32 h) : _buffer((size_t)(w * h)), _dimensions(w, h) {}

		void clear(Color4 color) {
			for (size_t i = 0; i < _dimensions.area(); ++i) {
				_buffer.set(i, color);
			}
		}

		void clear() {
			_buffer.clear();
		}

		Color4* getData() {
			return _buffer.data();
		}

		Color4Buffer& getBuffer() {
			return _buffer;
		}

		const Color4Buffer& getBuffer() const {
			return _buffer;
		}

		Color4Buffer getRow(int32 rowIdx) {
			return _buffer.slice(rowIdx * _dimensions.w, _dimensions.w);
		}

		const Color4* getData() const {
			return _buffer.data();
		}

		void write(const Color4Buffer& source) {
			write(source.data(), source.size());
		}

		void write(const Color4* data, size_t size) {
			_buffer.write(data, size);
		}

		/*ImageView view(Rect area) {
			area.w = area.w > 0 ? area.w : _dimensions.w - area.x;
			area.h = area.h > 0 ? area.h : _dimensions.h - area.y;

			assert(area.w > 0);
			assert(area.h > 0);
			assert(area.x + area.w <= _dimensions.w);
			assert(area.y + area.h <= _dimensions.h);

			return ImageView(&_buffer, area);
		}*/

		Color4 getPixel(int32 x, int32 y) {
			assert(x < _dimensions.w);
			assert(y < _dimensions.h);
			return _buffer.get(y * _dimensions.w + x);
		}

		void setPixel(int32 x, int32 y, Color4 v) {
			assert(x < _dimensions.w);
			assert(y < _dimensions.h);
			_buffer.set(y * _dimensions.w + x, v);
		}

		void setPixel(int32 x, int32 y, Color3 v) {
			setPixel(x, y, Color4(v.r, v.g, v.b, 255));
		}

		Dimension dimensions() const {
			return _dimensions;
		}

		Rect area() const {
			return Rect(0, 0, w(), h());
		}

		int32 w() const {
			return _dimensions.w;
		}

		int32 h() const {
			return _dimensions.h;
		}
	};

	using ImagePtr = std::shared_ptr<Image>;

	class ImageView {
	private:
		Image* _image = nullptr;
		Rect _area;

	public:
		ImageView() {}
		ImageView(Image* image) : _image(image), _area(image->area()) {}
		ImageView(Image* image, Rect area) : _image(image), _area(area) {}

		void clear(Color4 color) {
			for (int32 y = 0; y < _area.h; y++) {
				for (int32 x = 0; x < _area.w; x++) {
					setPixel(x, y, color);
				}
			}
		}

		ImageView view(Rect area = Rect()) {
			area.w = area.w > 0 ? area.w : _area.w - area.x;
			area.h = area.h > 0 ? area.h : _area.h - area.y;

			assert(area.x + area.w <= _area.w);
			assert(area.y + area.h <= _area.h);

			area.x += _area.x;
			area.y += _area.y;

			return ImageView(_image, area);
		}

		Color4 getPixel(int32 x, int32 y) {
			assert(x < _area.w);
			assert(y < _area.h);

			x += _area.x;
			y += _area.y;

			return _image->getPixel(x, y);
		}

		void setPixel(int32 x, int32 y, Color4 v) {
			assert(x < _area.w);
			assert(y < _area.h);

			x += _area.x;
			y += _area.y;

			_image->setPixel(x, y, v);
		}

		void setPixel(int32 x, int32 y, Color3 v) {
			assert(x < _area.w);
			assert(y < _area.h);

			x += _area.x;
			y += _area.y; 

			_image->setPixel(x, y, v);
		}

		const Rect& area() const {
			return _area;
		}

		int32 w() const {
			return _area.w;
		}

		int32 h() const {
			return _area.h;
		}
	};
}
