#pragma once

#include <yoga/Yoga.h>

#include "foundation/LuaReflection.h"
#include "ui/ButtonView.h"
#include "ui/DropDownMenuView.h"
#include "ui/ObjectInspectorUtil.h"
#include "ui/PanelView.h"
#include "ui/PropertyEditorView.h"
#include "ui/Splitterview.h"
#include "ui/SliderView.h"
#include "ui/VerticalSplitter.h"
#include "ui/View.h"
#include "application/Application.h"

using namespace fw;

int reflTest();

namespace fw {	
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
	
	class NewUi : public View {
		RegisterObject()
	private:
		PropertyEditorViewPtr _propGrid;
		PanelViewPtr _panel;
		std::shared_ptr<SplitterView> _splitter;
		lua_State* _lua;

	public:
		NewUi() : View({ 1024, 768 }) {
			reflTest();
			setFocusPolicy(FocusPolicy::Click);
			getLayout().setDimensions(100_pc);

			/*_lua = luaL_newstate();
			luaL_requiref(_lua, "_G", luaopen_base, 1);
			lua_pop(_lua, 1);*/

			_lua = luaL_newstate();   /* opens Lua */
			luaopen_base(_lua);             /* opens the basic library */
			luaopen_table(_lua);            /* opens the table library */
			luaopen_io(_lua);               /* opens the I/O library */
			luaopen_string(_lua);           /* opens the string lib. */
			luaopen_math(_lua);             /* opens the math lib. */

			LuaReflection::addClass<PointF32>(_lua);
			LuaReflection::addClass<PointI32>(_lua);
			LuaReflection::addClass<ViewLayout>(_lua);
			LuaReflection::addClass<View>(_lua);
			
			luaL_dofile(_lua, "E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\ui\\main.lua");
		}

		~NewUi() {
			lua_close(_lua);
		}

		void onInitialize() override {
			getLayout().setDimensions(100_pc);
			
			_panel = addChild<PanelView>("Edit Panel");
			_panel->setColor(Color4F::blue);
			_panel->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			_panel->getLayout().setDimensions(100_pc);
			_panel->getLayout().setLayoutDirection(LayoutDirection::LTR);
			_panel->getLayout().setFlexDirection(FlexDirection::Row);
			_panel->getLayout().setJustifyContent(FlexJustify::FlexStart);
			_panel->getLayout().setFlexAlignItems(FlexAlign::FlexStart);
			_panel->getLayout().setFlexAlignContent(FlexAlign::Stretch);

			/*auto text = panel->addChild<TextEditView>("TextEdit");
			text->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			text->getLayout().setPosition(FlexEdge::Top, 100);
			text->getLayout().setPosition(FlexEdge::Left, 100);
			text->getLayout().setDimensions(Dimension{ 200, 40 });*/

			auto propGrid = _panel->addChild<PropertyEditorView>("PropertyEditorView");
			propGrid->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			propGrid->getLayout().setPositionEdge(FlexEdge::Top, 100);
			propGrid->getLayout().setPositionEdge(FlexEdge::Left, 100);
			propGrid->getLayout().setDimensions(Dimension{ 400, 600 });
			
			ObjectInspectorUtil::reflect(propGrid, *_panel);
			
			auto c1 = _panel->addChild<PanelView>("C1");
			c1->setColor(Color4F::darkGrey);
			c1->getLayout().setDimensions(Dimension{ 75, 75 });
			auto c2 = _panel->addChild<PanelView>("C2");
			c2->setColor(Color4F::lightGrey);
			c2->getLayout().setDimensions(Dimension{ 75, 75 });
			auto c3 = _panel->addChild<PanelView>("C3");
			c3->setColor(Color4F::darkGrey);
			c3->getLayout().setDimensions(Dimension{ 75, 75 });
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

REFL_AUTO(
	type(fw::NewUi, bases<fw::View>)
)
