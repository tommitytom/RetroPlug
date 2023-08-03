#pragma once

#include "foundation/MathUtil.h"
#include "ui/View.h"
#include "graphics/Texture.h"

namespace fw {
	struct ControllerChangedEvent {
		f32 value = 0.0f;
	};

	class KnobView : public View {
		RegisterObject()
	private:
		f32 _value = 0.0f;
		f32 _stepSize = 0.01f;

		f32 _min = 0.0f;
		f32 _max = 1.0f;

		TextureHandle _texture;
		Dimension _tileSize;
		int32 _tileCount = 0;

		Point _clickPos;
		f32 _clickValue = 0.0f;
		bool _mouseDown = false;

	public:
		KnobView() {
			setType<KnobView>();
			setFocusPolicy(FocusPolicy::Click);
		}

		void setTexture(TextureHandle texture, uint32 tileCount) {
			_texture = texture;
			_tileCount = (int32)tileCount;

			_tileSize = texture.getResource().getDesc().dimensions;
			_tileSize.h /= _tileCount;

			getLayout().setDimensions(_tileSize);
		}

		void setRange(f32 min, f32 max) {
			_min = min;
			_max = max;
			_value = MathUtil::clamp(_value, min, max);
		}

		Dimension getTileSize() const {
			return _tileSize;
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point pos) override {
			if (button == MouseButton::Left) {
				_clickPos = pos;
				_mouseDown = down;
				_clickValue = _value;
			}

			return true;
		}

		bool onMouseMove(Point pos) override {
			if (_mouseDown) {
				Point dist = _clickPos - pos;
				_value = MathUtil::clamp(_clickValue + (f32)dist.y * _stepSize, 0.0f, 1.0f);
				emit(ControllerChangedEvent{ getValue() });
			}

			return true;
		}

		f32 getValue() const {
			return _value * (_max - _min) + _min;
		}

		void setValue(f32 value) {
			_value = (value - _min) / (_max - _min);
		}

		void onRender(fw::Canvas& canvas) override {
			int32 tileOffset = (int32)(_value * (_tileCount - 1));
			Rect tile(0, tileOffset * _tileSize.h, _tileSize.w, _tileSize.h);
			f32 h = (f32)_texture.getResource().getDesc().dimensions.h;

			TileArea tileArea = {
				.top = tile.y / h,
				.left = 0.0f,
				.bottom = tile.bottom() / h,
				.right = 1.0f
			};

			canvas.texture(_texture, getDimensionsF(), tileArea, Color4F(1, 1, 1, getAlpha()));
		}
	};
}

REFL_AUTO(
	type(fw::KnobView, bases<fw::View>),
	func(getValue, property("value")), func(setValue, property("value"))
)
