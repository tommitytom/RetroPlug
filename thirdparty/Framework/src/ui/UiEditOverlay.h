#pragma once

#include "ui/ObjectInspectorView.h"

namespace orb {
	class UiEditOverlay : public orb::View {
		FwRegisterObject();
	private:
		std::weak_ptr<orb::View> _view;
		std::weak_ptr<orb::View> _mouseOver;
		orb::ObjectInspectorViewPtr _inspector;
		const orb::TypeRegistry& _typeRegistry;

	public:
		UiEditOverlay(const orb::TypeRegistry& typeRegistry, orb::ObjectInspectorViewPtr inspector) : _typeRegistry(typeRegistry), _inspector(inspector) {
			setName("UI Edit Overlay");
		}

		void setView(std::weak_ptr<orb::View> view) {
			_view = view;
		}

		bool onMouseButton(const orb::MouseButtonEvent& ev) override {
			if (_view.expired() || !ev.down || ev.button != orb::MouseButton::Left) {
				return false;
			}

			uint32 depth = 0;
			std::shared_ptr<orb::View> view = viewAt(_view.lock(), (orb::PointF)ev.position, depth);
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

		bool onMouseMove(orb::Point pos) override {
			if (_view.expired()) {
				return false;
			}

			uint32 depth = 0;
			std::shared_ptr<orb::View> view = viewAt(_view.lock(), (orb::PointF)pos, depth);
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

		void onRender(orb::Canvas& canvas) override {
			auto over = _mouseOver.lock();
			if (!over) {
				return;
			}

			orb::RectF area = over->getScaledAreaF();
			canvas.strokeRect(area, orb::Color4F(0, 1, 0, 1));
		}

		orb::ViewPtr viewAt(const orb::ViewPtr& view, orb::PointF pos, uint32& depth) {
			for (auto child : view->getChildren()) {
				if (child->getScaledAreaF().contains(pos)) {
					orb::ViewPtr found = viewAt(child, pos - (view->getPositionF() * view->getWorldScale()), depth);

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
