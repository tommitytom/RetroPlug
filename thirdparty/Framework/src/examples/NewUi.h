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
	using PropFunc = void(*)(PropertyEditorViewPtr, entt::any&);
	/*const std::unordered_map<entt::id_type, PropFunc> PROP_FUNCS = {
		{ entt::type_hash<ViewLayout>::value(), &ObjectInspectorUtil::reflectAny<ViewLayout> }
	};

	struct MyMemberInfo {
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
		PropertyEditorViewPtr _propGrid;
			
	public:
		EditOverlay() {
			setType<EditOverlay>();
		}

		void setPropGrid(PropertyEditorViewPtr view) {
			_propGrid = view;
		}

		void setSelected(ViewPtr view) {
			_selected = view;

			_propGrid->clearProperties();
			
			ObjectInspectorUtil::reflect(_propGrid, view->getLayout());
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			if (!_active) {
				//return false;
			}
			
			if (ev.button == MouseButton::Left && ev.down) {
				//_view->hitTest();
			}

			return true;
		}

		void onRender(fw::Canvas& canvas) override {
			if (!_selected) {
				return;
			}

			Rect area = _selected->getWorldArea();
			area.position -= getWorldPosition();

			canvas.strokeRect(RectF(area), Color4F::red);
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
			_splitter->getLayout().setDimensions(FlexDimensionValue{
				FlexValue(FlexUnit::Percent, 100.0f),
				FlexValue(FlexUnit::Percent, 100.0f)
			});

			_propGrid = _splitter->addChild<PropertyEditorView>("Property Grid");
			_splitter->setLeft(_propGrid);

			_panel = _splitter->addChild<PanelView>("Panel");
			_panel->setColor(Color4F::blue);
			_splitter->setRight(_panel);
			
			_splitter->setSplitPercentage(0.25f);

			auto text = _panel->addChild<TextEditView>("TextEdit");
			text->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			text->getLayout().setPosition(FlexEdge::Top, 10);
			text->getLayout().setPosition(FlexEdge::Left, 10);
			text->getLayout().setDimensions(Dimension{ 200, 40 });
			
			auto panel = _panel->addChild<PanelView>("Panel");
			panel->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			panel->getLayout().setPosition(FlexEdge::Top, 100);
			panel->getLayout().setPosition(FlexEdge::Left, 100);
			panel->getLayout().setDimensions(Dimension{500, 500});
			panel->setColor(Color4F::white);

			panel->getLayout().setLayoutDirection(LayoutDirection::LTR);
			panel->getLayout().setFlexDirection(FlexDirection::Row);
			panel->getLayout().setJustifyContent(FlexJustify::FlexStart);
			panel->getLayout().setFlexAlignItems(FlexAlign::FlexStart);
			panel->getLayout().setFlexAlignContent(FlexAlign::Stretch);
			
			auto c1 = panel->addChild<PanelView>("C1");
			c1->setColor(Color4F::darkGrey);
			c1->getLayout().setDimensions(Dimension{ 75, 75 });
			auto c2 = panel->addChild<PanelView>("C2");
			c2->setColor(Color4F::lightGrey);
			c2->getLayout().setDimensions(Dimension{ 75, 75 });

			auto overlay = _panel->addChild<EditOverlay>("Edit overlay");
			overlay->setPropGrid(_propGrid);
			overlay->setSelected(panel);
			overlay->getLayout().setDimensions(FlexDimensionValue{
				FlexValue(FlexUnit::Percent, 100.0f),
				FlexValue(FlexUnit::Percent, 100.0f)
			});

			getLayout().setDimensions(FlexDimensionValue{
				FlexValue(FlexUnit::Percent, 100.0f),
				FlexValue(FlexUnit::Percent, 100.0f)
			});
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
		}

		void onRender(fw::Canvas& canvas) override {
			canvas.fillRect(getDimensionsF(), Color4F::red);
		}
	};

	using NewUiApplication = fw::app::BasicApplication<NewUi, void>;
}
