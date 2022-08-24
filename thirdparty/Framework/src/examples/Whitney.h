#pragma once

#include "foundation/Math.h"
#include "ui/View.h"

namespace fw {
	struct Dot {
		Point pos;
		RectF area;
		Color4F color;
	};

	class Whitney : public View {
	private:
		f32 _duration = 3600;
		f32 _position = 0.0f;
		TextureHandle _circle;

		std::vector<Dot> _dots;

	public:
		Whitney() : View({ 1024, 768 }) { 
			setType<Whitney>(); 
			setSizingPolicy(SizingPolicy::FitToParent);
		}
		~Whitney() = default;

		void onInitialize() override {
			_circle = getResourceManager().load<Texture>("C:\\code\\RetroPlugNext\\thirdparty\\Framework\\resources\\textures\\circle-512.png");

			for (size_t i = 0; i < 20000; ++i) {
				_dots.push_back(Dot{
					.color = Color4F::white
				});
			}
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			if (button == MouseButton::Left && down) {
				for (size_t i = 0; i < 20; ++i) {
					_dots.push_back(Dot{
						.color = Color4F::white
					});
				}
			} else if (button == MouseButton::Right && down) {
				if (_dots.size() > 20) {
					_dots.erase(_dots.end() - 20);
				}
			}

			return true;
		}

		void getColor(f32 ratio, f32 alpha, Color4F& v) {
			f32 r = ratio * PI2;
			v.r = (0.5f + cos(r) * 0.5f);
			v.g = (0.5f + cos(r + PI2 / 3.0f) * 0.5f);
			v.b = (0.5f + cos(r + PI2 * 2.0f / 3.0f) * 0.5f);
			v.a = alpha;
		}

		void onUpdate(f32 delta) override {
			_position += delta;
			f32 phase = (_position / _duration) * PI2;
			
			f32 size = 50.0f;
			f32 halfSize = size / 2;

			PointF mid((f32)getDimensions().w / 2, (f32)getDimensions().h / 2);

			f32 spacing = mid.y / _dots.size();

			for (size_t i = 0; i < _dots.size(); ++i) {
				f32 ratio = (f32)i / (f32)(_dots.size() - 1);
				f32 dist = spacing * (f32)(i + 1);
				f32 dotPhase = phase * (f32)((_dots.size()) - i);

				PointF pos(cos(dotPhase), sin(dotPhase));
				pos *= dist;
				pos += mid;
				pos -= halfSize;

				_dots[i].area = RectF(pos, { size, size });
				getColor(ratio, 0.5f, _dots[i].color);
			}
		}

		void onRender(Canvas& canvas) override {
			for (size_t i = 0; i < _dots.size(); ++i) {
				canvas.texture(_circle, _dots[i].area, _dots[i].color);
			}
		}
	};
}
