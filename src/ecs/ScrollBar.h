#pragma once

#include "ui/View.h"

namespace rp {
	struct ScrollBarEvent {
		f32 position;
	};

	class ScrollBar : public fw::View {
		FwRegisterObject()
	private:
		struct DragContext {
			bool active = false;
			fw::Point startPosition;
			f32 startScrollPosition = 0.0f;
		} _dragContext;

		f32 _scrollSize = 0.5;
		f32 _scrollPosition = 0.0;

	public:
		ScrollBar() {
			setFocusPolicy(fw::FocusPolicy::Click);
		}

		void setSize(f32 size) {
			_scrollSize = std::clamp(size, 0.0f, 1.0f);
		}

		void setPosition(f32 position) {
			_scrollPosition = std::clamp(position, 0.0f, 1.0f);
		}

		bool onMouseButton(const fw::MouseButtonEvent& ev) override {
			if (ev.button == fw::MouseButton::Left) {
				if (ev.down) {
					_dragContext.active = true;
					_dragContext.startPosition = ev.position;
					_dragContext.startScrollPosition = _scrollPosition;
				} else {
					_dragContext.active = false;
				}

				return true;
			}

			return false;
		}

		bool onMouseMove(fw::Point pos) override {
			if (_dragContext.active) {
				fw::DimensionF dim = getDimensionsF();
				f32 scrollSize = std::round(dim.h * _scrollSize);
				f32 deltaY = (f32)(pos.y - _dragContext.startPosition.y);
				f32 scrollableHeight = dim.h - scrollSize;
				if (scrollableHeight > 0) {
					f32 scrollDelta = deltaY / scrollableHeight;
					setPosition(_dragContext.startScrollPosition + scrollDelta);
					emit(ScrollBarEvent{ _scrollPosition });
				}

				return true;
			}

			return false;
		}

		void onRender(fw::Canvas& canvas) override {
			fw::DimensionF dim = getDimensionsF();
			canvas.fillRect(dim, fw::Color4F::darkGrey);

			f32 scrollSize = std::round(dim.h * _scrollSize);
			f32 scrollPos = std::round((dim.h - scrollSize) * _scrollPosition);

			canvas.fillRect(fw::RectF(0, scrollPos, dim.w, scrollSize), fw::Color4F::lightGrey);
		}
	};

	using ScrollBarPtr = std::shared_ptr<ScrollBar>;
}
