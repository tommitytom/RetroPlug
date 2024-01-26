#pragma once

#include "ui/ObjectInspectorView.h"

namespace fw {
	class UiEditOverlay : public fw::View {
		RegisterObject();
	private:
		std::weak_ptr<fw::View> _view;
		std::weak_ptr<fw::View> _mouseOver;
		fw::ObjectInspectorViewPtr _inspector;
		const fw::TypeRegistry& _typeRegistry;

	public:
		UiEditOverlay(const fw::TypeRegistry& typeRegistry, fw::ObjectInspectorViewPtr inspector) : _typeRegistry(typeRegistry), _inspector(inspector) {
			setName("UI Edit Overlay");
		}

		void setView(std::weak_ptr<fw::View> view) {
			_view = view;
		}

		bool onMouseButton(const fw::MouseButtonEvent& ev) override {
			if (_view.expired() || !ev.down || ev.button != fw::MouseButton::Left) {
				return false;
			}

			uint32 depth = 0;
			std::shared_ptr<fw::View> view = viewAt(_view.lock(), (fw::PointF)ev.position, depth);
			if (!view) {
				_mouseOver.reset();
				return false;
			}

			spdlog::info("Selected view: {}", view->getName());

			_mouseOver = view;
			_inspector->clear();
			_inspector->addView(_typeRegistry, view);

			return true;
		}

		bool onMouseMove(fw::Point pos) override {
			if (_view.expired()) {
				return false;
			}

			uint32 depth = 0;
			std::shared_ptr<fw::View> view = viewAt(_view.lock(), (fw::PointF)pos, depth);
			if (!view) {
				_mouseOver.reset();
				return false;
			}

			_mouseOver = view;
			return true;
		}

		void onMouseLeave() override {
			_mouseOver.reset();
		}

		void onRender(fw::Canvas& canvas) override {
			auto over = _mouseOver.lock();
			if (!over) {
				return;
			}

			fw::RectF area = over->getScaledAreaF();
			canvas.strokeRect(area, fw::Color4F(0, 1, 0, 1));
		}

		fw::ViewPtr viewAt(const fw::ViewPtr& view, fw::PointF pos, uint32& depth) {
			for (auto child : view->getChildren()) {
				if (child->getScaledAreaF().contains(pos)) {
					fw::ViewPtr found = viewAt(child, pos - (view->getPositionF() * view->getWorldScale()), depth);

					if (found) {
						return found;
					}

					return child;
				}
			}

			return nullptr;
		}
	};
}
