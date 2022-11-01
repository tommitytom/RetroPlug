#pragma once

#include "ui/View.h"

namespace fw {
	class PlotView final : public View {
	public:
		using Func = f32(*)(f32);

		struct Theme {
			Color4 foreground = Color4(255, 255, 255, 255);
			Color4 background = Color4(0, 0, 0, 255);
			Color4 selection = Color4(255, 0, 0, 255);
			Color4 waveform = Color4(75, 243, 167, 255);
			Color4 waveformSelected = Color4(19, 60, 41, 255);
		};

	private:
		Func _func = nullptr;
		Theme _theme;
		std::vector<PointF> _points;

	public:
		PlotView() {
			setType<PlotView>();
			setFocusPolicy(FocusPolicy::Click);
		}
		~PlotView() {}

		void onInitialize() override { updatePoints(getDimensions()); }
		void onResize(const ResizeEvent& ev) override { updatePoints(ev.size); }

		void onRender(Canvas& canvas) override {
			canvas.fillRect(getDimensionsF(), _theme.background);

			if (_points.size() > 2) {
				canvas.lines(_points, _theme.waveform);
			}
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point pos) override {
			updatePoints(getDimensions());
			return true;
		}

		void onUpdate(f32 delta) override {
			updatePoints(getDimensions());
		}

		void setTheme(const PlotView::Theme& theme) {
			_theme = theme;
		}

		void setFunc(Func f) {
			_func = f;
			updatePoints(getDimensions());
		}

	private:
		void updatePoints(Dimension res) {
			_points.clear();

			if (_func) {
				_points.resize(res.w);

				f32 w = (f32)res.w - 1.0f;

				for (int32 i = 0; i < res.w; ++i) {
					f32 ratio = (f32)i / w;

					_points[i] = {
						.x = (f32)i,
						.y = res.h - (_func(ratio) * res.h)
					};
				}
			}
		}
	};

	using PlotViewPtr = std::shared_ptr<PlotView>;
}
