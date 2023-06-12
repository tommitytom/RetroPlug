#pragma once


#include <yoga/Yoga.h>

#include "ui/ButtonView.h"
#include "ui/View.h"
#include "ui/VerticalSplitter.h"
#include "ui/PropertyEditorView.h"
#include "ui/ObjectInspectorUtil.h"
#include "ui/SliderView.h"
#include "ui/DropDownMenuView.h"
#include "application/Application.h"

using namespace fw;

namespace fw {
	using PropAnyFunc = void(*)(PropertyEditorViewPtr, entt::any&);
	using PropObjectFunc = void(*)(PropertyEditorViewPtr, fw::Object&);

	const std::unordered_map<entt::id_type, PropObjectFunc> PROP_OBJECT_FUNCS = {
		{ entt::type_hash<View>::value(), &ObjectInspectorUtil::reflectObject<View> },
		{ entt::type_hash<LabelView>::value(), &ObjectInspectorUtil::reflectObject<LabelView> },
		{ entt::type_hash<PanelView>::value(), &ObjectInspectorUtil::reflectObject<PanelView> }
	};
	
	/*const std::unordered_map<entt::id_type, PropAnyFunc> PROP_ANY_FUNCS = {
		{ entt::type_hash<ViewLayout>::value(), &ObjectInspectorUtil::reflectAny<ViewLayout> }
	};*/

	/*struct MyMemberInfo {
		std::string_view name;
	};

	constexpr auto members = refl::util::map_to_array<MyMemberInfo>(refl::member_list<ViewLayout>{}, [](auto m) {
		return MyMemberInfo{
			.name = get_name(m).c_str()
		};
	});*/

	class EditOverlay : public View {
	private:
		ViewPtr _view;
		bool _active = false;

		ViewPtr _selected;
		ViewPtr _highlighted;
		PropertyEditorViewPtr _propGrid;
			
	public:
		EditOverlay() {
			setType<EditOverlay>();
		}

		void setPropGrid(PropertyEditorViewPtr view) {
			_propGrid = view;
		}

		void setEditRoot(ViewPtr view) {
			_view = view;
		}

		void setSelected(ViewPtr view) {
			_selected = view;

			_propGrid->clearProperties();
			_propGrid->pushGroup(std::string_view("General"));
			
			uint32 typeId = view->getTypeId();

			auto found = PROP_OBJECT_FUNCS.find(view->getTypeId());
			if (found != PROP_OBJECT_FUNCS.end()) {
				found->second(_propGrid, *view);
			} else {
				ObjectInspectorUtil::reflect<View>(_propGrid, *view);
			}
		}

		bool onMouseMove(Point pos) override {
			if (/*!_active || */!_view) {
				return false;
			}

			_highlighted = getChildAtPoint(_view, PointF(pos), true);

			return true;
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			if (/*!_active || */!_view) {
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

	class Splitter : public View {
	private:
		ViewPtr _left;
		ViewPtr _right;
		f32 _splitPos = 0.5f;

	public:
		Splitter() {
			setType<Splitter>();
			getLayout().setLayoutDirection(LayoutDirection::LTR);
			getLayout().setFlexDirection(FlexDirection::Row);
			getLayout().setJustifyContent(FlexJustify::FlexStart);
			getLayout().setFlexAlignItems(FlexAlign::FlexStart);
			getLayout().setFlexAlignContent(FlexAlign::Stretch);
		}

		void setSplitPercentage(f32 perc) {
			_splitPos = perc;

			if (_left) {
				_left->getLayout().setDimensions(FlexDimensionValue{
					FlexValue(FlexUnit::Percent, _splitPos * 100.0f),
					FlexValue(FlexUnit::Percent, 100.0f)
				});
			}
			
			if (_right) {
				_right->getLayout().setDimensions(FlexDimensionValue{
					FlexValue(FlexUnit::Percent, (1.0f - _splitPos) * 100.0f),
					FlexValue(FlexUnit::Percent, 100.0f)
				});
			}			
		}
		
		void setLeft(ViewPtr view) {
			_left = view;
			setSplitPercentage(_splitPos);
		}

		void setRight(ViewPtr view) {		
			_right = view;
			setSplitPercentage(_splitPos);
		}
	};

	class NewUi : public View {
	private:
		PropertyEditorViewPtr _propGrid;
		PanelViewPtr _panel;
		std::shared_ptr<Splitter> _splitter;

	public:
		NewUi() : View({ 1024, 768 }) {
			setType<NewUi>();
			setFocusPolicy(FocusPolicy::Click);
		}

		~NewUi() = default;

		void onInitialize() override {
			_splitter = addChild<Splitter>("Container");
			_splitter->getLayout().setDimensions(100_pc);

			_propGrid = _splitter->addChild<PropertyEditorView>("Property Grid");
			_splitter->setLeft(_propGrid);

			_panel = _splitter->addChild<PanelView>("Right Panel");
			_splitter->setRight(_panel);
			
			_splitter->setSplitPercentage(0.25f);

			auto panel = _panel->addChild<PanelView>("Edit Panel");
			panel->setColor(Color4F::blue);
			panel->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			panel->getLayout().setDimensions(100_pc);
			panel->getLayout().setLayoutDirection(LayoutDirection::LTR);
			panel->getLayout().setFlexDirection(FlexDirection::Row);
			panel->getLayout().setJustifyContent(FlexJustify::FlexStart);
			panel->getLayout().setFlexAlignItems(FlexAlign::FlexStart);
			panel->getLayout().setFlexAlignContent(FlexAlign::Stretch);

			/*auto text = panel->addChild<TextEditView>("TextEdit");
			text->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			text->getLayout().setPosition(FlexEdge::Top, 100);
			text->getLayout().setPosition(FlexEdge::Left, 100);
			text->getLayout().setDimensions(Dimension{ 200, 40 });*/

			auto propGrid = panel->addChild<PropertyEditorView>("PropertyEditorView");
			propGrid->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			propGrid->getLayout().setPositionEdge(FlexEdge::Top, 100);
			propGrid->getLayout().setPositionEdge(FlexEdge::Left, 100);
			propGrid->getLayout().setDimensions(Dimension{ 400, 600 });
			
			ObjectInspectorUtil::reflect(propGrid, *panel);
			
			auto c1 = panel->addChild<PanelView>("C1");
			c1->setColor(Color4F::darkGrey);
			c1->getLayout().setDimensions(Dimension{ 75, 75 });
			auto c2 = panel->addChild<PanelView>("C2");
			c2->setColor(Color4F::lightGrey);
			c2->getLayout().setDimensions(Dimension{ 75, 75 });
			auto c3 = panel->addChild<PanelView>("C3");
			c3->setColor(Color4F::darkGrey);
			c3->getLayout().setDimensions(Dimension{ 75, 75 });

			auto overlay = _panel->addChild<EditOverlay>("Edit overlay");
			overlay->setEditRoot(panel);
			overlay->setPropGrid(_propGrid);
			overlay->setSelected(panel);
			overlay->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			overlay->getLayout().setDimensions(100_pc);

			getLayout().setDimensions(100_pc);
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			if (ev.button == MouseButton::Left && ev.down) {
				//auto button = _root->addChild<ButtonView>(fmt::format("Button{}", _root->getChildren().size() + 1));
				//button->setText(button->getName());
			}

			return true;
		}

		bool onKey(const KeyEvent& ev) override {
			return false;
		}

		void onUpdate(f32 delta) override {
			_panel->setName("Edit Panel");
		}

		void onRender(fw::Canvas& canvas) override {
			canvas.fillRect(getDimensionsF(), Color4F::red);
		}
	};

	using NewUiApplication = fw::app::BasicApplication<NewUi, void>;
}
