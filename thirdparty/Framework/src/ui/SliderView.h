#pragma once

#include "ui/View.h"
#include "foundation/MathUtil.h"
#include "ui/Property.h"
#include "foundation/Curves.h"

namespace fw {
	struct SliderTheme {
		uint32 handleWidth = 4;
	};

	struct SliderChangeEvent {
		f32 value = 0.0f;
	};

	class SliderView : public TypedPropertyEditor<f32> {
	private:
		f32 _min = 0;
		f32 _max = 1;
		f32 _stepSize = 0;

		Curves::Func _curve = Curves::linear;
		bool _showValueLabel = true;

		std::vector<f32> _values = { 0.0f };
		std::vector<RectF> _handleAreas = { RectF() };
		size_t _lastEditedValue = 0;

		bool _dragging = false;
		bool _mouseOverHandle = false;

		f32 _handleRange = 0.0f;
		std::string _labelFormat = "{}";

		bool _editable = true;

	public:
		std::function<void(f32)> ValueChangeEvent;

		SliderView() {
			setType<SliderView>();
			getLayout().setDimensions(Dimension{ 200, 30 });
			setFocusPolicy(FocusPolicy::Click);
		}
		~SliderView() = default;

		void setRange(f32 min, f32 max) {
			_min = min;
			_max = max;

			if (ValueChangeEvent) {
				ValueChangeEvent(getValue());
			}
		}

		void setStepSize(f32 stepSize) {
			_stepSize = stepSize;
		}

		void setHandleCount(size_t handleCount) {
			assert(handleCount > 0 && handleCount < 4);

			while (handleCount < _handleAreas.size()) {
				_handleAreas.pop_back();
				_values.pop_back();
			}

			while (handleCount > _handleAreas.size()) {
				_handleAreas.push_back(RectF());
				_values.push_back((_max - _min) / 2 + _min);
			}

			_lastEditedValue = std::min(_lastEditedValue, handleCount - 1);
		}

		void setValue(const f32& value) override {
			setValueAt(0, value);
		}

		void setValueAt(size_t handleIdx, f32 value) {
			_values[handleIdx] = (MathUtil::clamp(value, _min, _max) - _min) / getRange();

			if (!_dragging) {
				_lastEditedValue = handleIdx;
			}

			if (isMounted()) {
				updateHandleArea();
			}
		}

		f32 getValueAt(size_t handleIdx) const {
			f32 val = MathUtil::clamp(_curve(_values[handleIdx]), 0.0f, 1.0f);
			f32 scaled = val * getRange();

			if (_stepSize > 0.0f) {
				scaled -= fmod(scaled, _stepSize);
			}

			return scaled + _min;
		}

		f32 getRange() const {
			return _max - _min;
		}

		f32 getValue() const override {
			return getValueAt(0);
		}

		void setCurve(Curves::Func&& func) {
			_curve = std::move(func);
		}

		void setCurve(const Curves::Func& func) {
			_curve = func;
		}

		void setLabelFormat(std::string_view format) {
			_labelFormat = std::string(format);
		}

		void onInitialize() override {
			updateHandleArea();
		}

		void setEditable(bool editable) {
			_editable = editable;
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point pos) override {
			if (!_editable) {
				return true;
			}

			if (button == MouseButton::Left) {
				_dragging = down;

				if (down) {
					_mouseOverHandle = true;
					dragHandle(pos);
				} else {
					_mouseOverHandle = _handleAreas[0].contains((PointF)pos);
				}
			}

			return true;
		}

		bool onMouseMove(Point pos) override {
			if (!_editable) {
				return true;
			}

			if (_dragging) {
				dragHandle(pos);
			} else {
				_mouseOverHandle = _handleAreas[0].contains((PointF)pos);
			}

			return true;
		}

		void onMouseLeave() override {
			_mouseOverHandle = _dragging;
		}

		void onUpdate(f32 delta) override {
			updateHandleArea();
		}

		void onRender(fw::Canvas& canvas) override {
			DimensionF dim = getDimensionsF();
			f32 mid = floor(dim.h * 0.5f);
			f32 half = (dim.w - _handleRange) * 0.5f;

			canvas
				.fillRect(dim, hasFocus() ? Color4F(0.3f, 0.3f, 0.3f, 1.0f) : Color4F::darkGrey);

			for (size_t i = _handleAreas.size() - 1; i >= 1; --i) {
				canvas.fillRect(_handleAreas[i], Color4F::black);
			}

			canvas
				.fillRect(RectF(0, 0, _handleAreas[0].x, dim.h), Color4F::lightGrey)
				.fillRect(_handleAreas[0], _mouseOverHandle ? Color4F(0.5f, 0.5f, 0.5f, 1.0f) : Color4F(0.6f, 0.6f, 0.6f, 1.0f));
				//.line( half, mid , half + _handleRange, mid, Color4F::green);

			if (_showValueLabel) {
				f32 value = getValueAt(_lastEditedValue);
				canvas.setTextAlign(TextAlignFlags::Left | TextAlignFlags::Middle);
				canvas.text(RectF({ 0, 0 }, dim), fmt::format(_labelFormat, value), fw::Color4F::black);
			}
		}

		void onMount() override {
			updateHandleArea();
		}

		void onResize(const ResizeEvent& ev) override {
			if (isMounted()) {
				updateHandleArea();
			}
		}

		void onLayoutChanged() override {
			if (isMounted()) {
				updateHandleArea();
			}
		}

	private:
		void dragHandle(Point pos) {
			DimensionF dim = getDimensionsF();
			f32 handleWidth = (f32)getTheme<SliderTheme>().handleWidth;

			dim.w -= handleWidth;
			pos.x -= (int32)(handleWidth / 2);

			_values[0] = MathUtil::clamp((f32)pos.x / dim.w, 0.0f, 1.0f);
			_lastEditedValue = 0;

			emit(SliderChangeEvent{ getValue() });

			if (ValueChangeEvent) {
				ValueChangeEvent(getValue());
			}

			updateHandleArea();
		}

		void updateHandleArea() {
			DimensionF dim = getDimensionsF();
			f32 handleWidth = (f32)getTheme<SliderTheme>().handleWidth;
			f32 halfHandleWidth = floor(handleWidth * 0.5f);

			_handleRange = dim.w - handleWidth;

			for (size_t i = 0; i < _values.size(); ++i) {
				_handleAreas[i] = RectF(_values[i] * _handleRange, 0, handleWidth, dim.h);
			}
		}
	};

	using SliderViewPtr = std::shared_ptr<SliderView>;
}
