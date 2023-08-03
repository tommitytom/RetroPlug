#pragma once

#include "ui/PropertyInspectors.h"
#include "ui/ObjectInspectorUtil.h"
#include "ui/PanelView.h"
#include "ui/SplitterView.h"

namespace fw {
	struct EditViewSelectedEvent {
		ViewPtr selected;
	};

	class EditOverlay : public View {
		RegisterObject()
	private:
		ViewPtr _view;
		ViewPtr _selected;
		ViewPtr _highlighted;

	public:
		EditOverlay() {
			setType<EditOverlay>();
			getLayout().setFlexPositionType(FlexPositionType::Absolute);
			getLayout().setDimensions(100_pc);
		}

		void setEditRoot(ViewPtr view) {
			_view = view;
			_selected = view;
		}

		void setSelected(ViewPtr view) {
			_selected = view;
			emit(EditViewSelectedEvent{ .selected = view });
		}

		bool onMouseMove(Point pos) override {
			if (!_view) {
				return false;
			}

			_highlighted = getChildAtPoint(_view, PointF(pos), true);

			return true;
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			if (!_view) {
				return false;
			}

			if (ev.button == MouseButton::Left && ev.down) {
				setSelected(getChildAtPoint(_view, PointF(ev.position), true));
			}

			return true;
		}

		void onRender(fw::Canvas& canvas) override {
			if (_highlighted) {
				Rect area = _highlighted->getWorldArea();
				area.position -= getWorldPosition();
				canvas.strokeRect(RectF(area), Color4F::green);
			}

			if (_selected) {
				Rect area = _selected->getWorldArea();
				area.position -= getWorldPosition();
				canvas.strokeRect(RectF(area), Color4F::red);
			}
		}

	private:
		ViewPtr getChildAtPoint(const ViewPtr& current, PointF point, bool recursive) {
			for (int32 i = (int32)current->getChildren().size() - 1; i >= 0; --i) {
				fw::ViewPtr child = current->getChildren()[i];
				PointF childPos = point - child->getPositionF();

				if (child->getDimensionsF().contains(childPos.x, childPos.y)) {
					if (recursive) {
						return getChildAtPoint(child, childPos, true);
					}

					return child;
				}
			}

			return current;
		}
	};

	using EditOverlayPtr = std::shared_ptr<EditOverlay>;

	class EditView : public View {
		RegisterObject()
			
	private:
		PropertyEditorViewPtr _propGrid;
		SplitterViewPtr _splitter;
		EditOverlayPtr _overlay;
		PanelViewPtr _panel;
		ViewPtr _view;
		bool _active = false;

	public:
		EditView() : View({ 1024, 768 }) { setType<EditView>(); }

		void onInitialize() override {
			addGlobalKeyHandler([&](const KeyEvent& ev) {
				if (ev.key == VirtualKey::F2) {
					if (ev.down) {
						setActive(!_active);
					}

					return true;
				}

				return false;
			});

			getLayout().setDimensions(100_pc);

			updateLayout();
		}
		
		void setActive(bool active) {
			_active = active;
			
			if (isInitialized()) {
				updateLayout();
			}
		}
		
		void setView(ViewPtr view) {
			_view = view;
			
			if (isInitialized()) {
				updateLayout();
			}
		}

	private:
		void updateLayout() {
			removeChildren();

			if (_active) {
				_splitter = addChild<SplitterView>("Container");
				_splitter->getLayout().setDimensions(100_pc);

				_propGrid = _splitter->addChild<PropertyEditorView>("Property Grid");
				_panel = _splitter->addChild<PanelView>("Edit Panel");

				_splitter->setLeft(_propGrid);
				_splitter->setRight(_panel);
				_splitter->setSplitPercentage(0.25f);

				_overlay = _panel->addChild<EditOverlay>("Edit overlay");
				
				if (_view) {
					_panel->addChild(_view);
					_overlay->setEditRoot(_view);
				}

				_overlay->bringToFront();

				subscribe<EditViewSelectedEvent>(_overlay, [&](const EditViewSelectedEvent& ev) {
					updatePropertyGrid(ev.selected);
				});
			} else {
				_propGrid = nullptr;
				_splitter = nullptr;
				_overlay = nullptr;
				_panel = nullptr;

				if (_view) {
					addChild(_view);
				}
			}
		}
		
		void updatePropertyGrid(ViewPtr view) {
			_propGrid->clearProperties();
			_propGrid->pushGroup(view->getTypeName());

			uint32 typeId = view->getTypeId();
			PropertyInspectors::PropObjectFunc inspector = PropertyInspectors::find(typeId);

			if (inspector) {
				inspector(_propGrid, *view);
			} else {
				ObjectInspectorUtil::reflect<View>(_propGrid, *view);
			}
		}
	};
	
	using EditViewPtr = std::shared_ptr<EditView>;
}

REFL_AUTO(
	type(fw::EditOverlay, bases<fw::View>)
)
