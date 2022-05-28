#pragma once

#include "platform/Types.h"
#include "util/DataBuffer.h"

namespace rp {
	class Image {
	private:
		Color4Buffer _buffer;
		DimensionT<uint32> _dimensions;

	public:
		Image() {}
		Image(DimensionT<uint32> dimensions) : _buffer((size_t)(dimensions.area())), _dimensions(dimensions) {}
		Image(uint32 w, uint32 h) : _buffer((size_t)(w * h)), _dimensions(w, h) {}

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

		Color4Buffer getRow(uint32 rowIdx) {
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

		/*ImageView view(RectT<uint32> area) {
			area.w = area.w > 0 ? area.w : _dimensions.w - area.x;
			area.h = area.h > 0 ? area.h : _dimensions.h - area.y;

			assert(area.w > 0);
			assert(area.h > 0);
			assert(area.x + area.w <= _dimensions.w);
			assert(area.y + area.h <= _dimensions.h);

			return ImageView(&_buffer, area);
		}*/

		Color4 getPixel(uint32 x, uint32 y) {
			assert(x < _dimensions.w);
			assert(y < _dimensions.h);
			return _buffer.get(y * _dimensions.w + x);
		}

		void setPixel(uint32 x, uint32 y, Color4 v) {
			assert(x < _dimensions.w);
			assert(y < _dimensions.h);
			_buffer.set(y * _dimensions.w + x, v);
		}

		void setPixel(uint32 x, uint32 y, Color3 v) {
			setPixel(x, y, Color4(v.r, v.g, v.b, 255));
		}

		DimensionT<uint32> dimensions() const {
			return _dimensions;
		}

		RectT<uint32> area() const {
			return RectT<uint32>(0, 0, w(), h());
		}

		uint32 w() const {
			return _dimensions.w;
		}

		uint32 h() const {
			return _dimensions.h;
		}
	};

	using ImagePtr = std::shared_ptr<Image>;

	class ImageView {
	private:
		Image* _image = nullptr;
		RectT<uint32> _area;

	public:
		ImageView() {}
		ImageView(Image* image) : _image(image), _area(image->area()) {}
		ImageView(Image* image, RectT<uint32> area) : _image(image), _area(area) {}

		void clear(Color4 color) {
			for (uint32 y = 0; y < _area.h; y++) {
				for (uint32 x = 0; x < _area.w; x++) {
					setPixel(x, y, color);
				}
			}
		}

		ImageView view(RectT<uint32> area = RectT<uint32>()) {
			area.w = area.w > 0 ? area.w : _area.w - area.x;
			area.h = area.h > 0 ? area.h : _area.h - area.y;

			assert(area.x + area.w <= _area.w);
			assert(area.y + area.h <= _area.h);

			area.x += _area.x;
			area.y += _area.y;

			return ImageView(_image, area);
		}

		Color4 getPixel(uint32 x, uint32 y) {
			assert(x < _area.w);
			assert(y < _area.h);

			x += _area.x;
			y += _area.y;

			return _image->getPixel(x, y);
		}

		void setPixel(uint32 x, uint32 y, Color4 v) {
			assert(x < _area.w);
			assert(y < _area.h);

			x += _area.x;
			y += _area.y;

			_image->setPixel(x, y, v);
		}

		void setPixel(uint32 x, uint32 y, Color3 v) {
			assert(x < _area.w);
			assert(y < _area.h);

			x += _area.x;
			y += _area.y; 

			_image->setPixel(x, y, v);
		}

		const RectT<uint32>& area() const {
			return _area;
		}

		uint32 w() const {
			return _area.w;
		}

		uint32 h() const {
			return _area.h;
		}
	};
}

