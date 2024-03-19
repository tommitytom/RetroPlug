#include "ReactView.h"

#include "foundation/Input.h"
#include "foundation/LuaReflection.h"
#include "ui/View.h"
#include "ui/next/DocumentRenderer.h"
#include "ui/next/LuaReact.h"
#include "ui/next/StyleCache.h"
#include "ui/next/StyleComponentsMeta.h"
#include "ui/next/StyleUtil.h"

#include "ui/next/ReactTextView.h"

namespace fw {
	f64 getTime() {
		std::chrono::high_resolution_clock::duration time = std::chrono::high_resolution_clock::now().time_since_epoch();
		return std::chrono::duration_cast<std::chrono::duration<f64>>(time).count();
	}

	ReactView::ReactView() : View({ 1024, 768 }) {
		setName("ReactView");
		getLayout().setDimensions(100_pc);
		setFocusPolicy(FocusPolicy::Click);

		_listener.setCallback([&](FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
			reload();
		});
	}

	ReactView::ReactView(const std::filesystem::path& path) : View({ 1024, 768 }) {
		setName("ReactView");
		getLayout().setDimensions(100_pc);
		setFocusPolicy(FocusPolicy::Click);
		setPath(path);

		_listener.setCallback([&](FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
			reload();
		});
	}

	ReactView::~ReactView() {

	}

	void ReactView::onUpdate(f32 dt) {
		_fileWatcher.update();
		_listener.update(dt);

		if (_lua) {
			luabridge::LuaRef cb = luabridge::getGlobal(_lua, "onFrame");
			luabridge::LuaResult result = cb(dt);

			if (!result) {
				spdlog::error(result.errorMessage());
			}
		}
	}

	void ReactView::setPath(const std::filesystem::path& path) {
		_path = path;

		std::string root = std::filesystem::path(path).remove_filename().string();
		//_fileWatcher = FW::FileWatcher();
		_fileWatcher.addWatch(root, &_listener, true);

		reload();
	}

	template <typename T>
	const char* getStyleName(std::string& buf) {
		std::string_view propName = T::PropertyName;
		buf = "set_";

		for (size_t i = 0; i < propName.size(); ++i) {
			if (propName[i] == '-') {
				buf.push_back(std::toupper(propName[i + 1]));
				i++;
			} else {
				buf.push_back(propName[i]);
			}
		}

		return buf.c_str();
	}

	void addStyleSetters(lua_State* lua) {
		auto c = luabridge::getGlobalNamespace(lua).beginNamespace("fw").beginClass<ReactStyle>("ReactStyle");

		std::string buf;

		c.addFunction("clear", &ReactStyle::clear);

		c.addFunction(getStyleName<styles::Cursor>(buf), &ReactStyle::addProperty<styles::Cursor>);
		c.addFunction(getStyleName<styles::Color>(buf), &ReactStyle::addProperty<styles::Color>);
		c.addFunction(getStyleName<styles::BackgroundColor>(buf), &ReactStyle::addProperty<styles::BackgroundColor>);

		c.addFunction(getStyleName<styles::MarginBottom>(buf), &ReactStyle::addProperty<styles::MarginBottom>);
		c.addFunction(getStyleName<styles::MarginTop>(buf), &ReactStyle::addProperty<styles::MarginTop>);
		c.addFunction(getStyleName<styles::MarginLeft>(buf), &ReactStyle::addProperty<styles::MarginLeft>);
		c.addFunction(getStyleName<styles::MarginRight>(buf), &ReactStyle::addProperty<styles::MarginRight>);

		c.addFunction(getStyleName<styles::PaddingBottom>(buf), &ReactStyle::addProperty<styles::PaddingBottom>);
		c.addFunction(getStyleName<styles::PaddingTop>(buf), &ReactStyle::addProperty<styles::PaddingTop>);
		c.addFunction(getStyleName<styles::PaddingLeft>(buf), &ReactStyle::addProperty<styles::PaddingLeft>);
		c.addFunction(getStyleName<styles::PaddingRight>(buf), &ReactStyle::addProperty<styles::PaddingRight>);

		c.addFunction(getStyleName<styles::BorderBottomWidth>(buf), &ReactStyle::addProperty<styles::BorderBottomWidth>);
		c.addFunction(getStyleName<styles::BorderTopWidth>(buf), &ReactStyle::addProperty<styles::BorderTopWidth>);
		c.addFunction(getStyleName<styles::BorderLeftWidth>(buf), &ReactStyle::addProperty<styles::BorderLeftWidth>);
		c.addFunction(getStyleName<styles::BorderRightWidth>(buf), &ReactStyle::addProperty<styles::BorderRightWidth>);
		c.addFunction(getStyleName<styles::BorderBottomColor>(buf), &ReactStyle::addProperty<styles::BorderBottomColor>);
		c.addFunction(getStyleName<styles::BorderTopColor>(buf), &ReactStyle::addProperty<styles::BorderTopColor>);
		c.addFunction(getStyleName<styles::BorderLeftColor>(buf), &ReactStyle::addProperty<styles::BorderLeftColor>);
		c.addFunction(getStyleName<styles::BorderRightColor>(buf), &ReactStyle::addProperty<styles::BorderRightColor>);
		c.addFunction(getStyleName<styles::BorderBottomStyle>(buf), &ReactStyle::addProperty<styles::BorderBottomStyle>);
		c.addFunction(getStyleName<styles::BorderTopStyle>(buf), &ReactStyle::addProperty<styles::BorderTopStyle>);
		c.addFunction(getStyleName<styles::BorderLeftStyle>(buf), &ReactStyle::addProperty<styles::BorderLeftStyle>);
		c.addFunction(getStyleName<styles::BorderRightStyle>(buf), &ReactStyle::addProperty<styles::BorderRightStyle>);

		// flex-flow
		c.addFunction(getStyleName<styles::FlexDirection>(buf), &ReactStyle::addProperty<styles::FlexDirection>);
		c.addFunction(getStyleName<styles::FlexWrap>(buf), &ReactStyle::addProperty<styles::FlexWrap>);

		c.addFunction(getStyleName<styles::FlexBasis>(buf), &ReactStyle::addProperty<styles::FlexBasis>);
		c.addFunction(getStyleName<styles::FlexGrow>(buf), &ReactStyle::addProperty<styles::FlexGrow>);
		c.addFunction(getStyleName<styles::FlexShrink>(buf), &ReactStyle::addProperty<styles::FlexShrink>);
		c.addFunction(getStyleName<styles::AlignItems>(buf), &ReactStyle::addProperty<styles::AlignItems>);
		c.addFunction(getStyleName<styles::AlignContent>(buf), &ReactStyle::addProperty<styles::AlignContent>);
		c.addFunction(getStyleName<styles::AlignSelf>(buf), &ReactStyle::addProperty<styles::AlignSelf>);
		c.addFunction(getStyleName<styles::JustifyContent>(buf), &ReactStyle::addProperty<styles::JustifyContent>);
		c.addFunction(getStyleName<styles::Overflow>(buf), &ReactStyle::addProperty<styles::Overflow>);

		c.addFunction(getStyleName<styles::Position>(buf), &ReactStyle::addProperty<styles::Position>);
		c.addFunction(getStyleName<styles::Top>(buf), &ReactStyle::addProperty<styles::Top>);
		c.addFunction(getStyleName<styles::Left>(buf), &ReactStyle::addProperty<styles::Left>);
		c.addFunction(getStyleName<styles::Bottom>(buf), &ReactStyle::addProperty<styles::Bottom>);
		c.addFunction(getStyleName<styles::Right>(buf), &ReactStyle::addProperty<styles::Right>);

		c.addFunction(getStyleName<styles::Width>(buf), &ReactStyle::addProperty<styles::Width>);
		c.addFunction(getStyleName<styles::Height>(buf), &ReactStyle::addProperty<styles::Height>);
		c.addFunction(getStyleName<styles::MinWidth>(buf), &ReactStyle::addProperty<styles::MinWidth>);
		c.addFunction(getStyleName<styles::MinHeight>(buf), &ReactStyle::addProperty<styles::MinHeight>);
		c.addFunction(getStyleName<styles::MaxWidth>(buf), &ReactStyle::addProperty<styles::MaxWidth>);
		c.addFunction(getStyleName<styles::MaxHeight>(buf), &ReactStyle::addProperty<styles::MaxHeight>);

		c.addFunction(getStyleName<styles::TransitionProperty>(buf), &ReactStyle::addProperty<styles::TransitionProperty>);
		c.addFunction(getStyleName<styles::TransitionDuration>(buf), &ReactStyle::addProperty<styles::TransitionDuration>);
		c.addFunction(getStyleName<styles::TransitionTimingFunction>(buf), &ReactStyle::addProperty<styles::TransitionTimingFunction>);
		c.addFunction(getStyleName<styles::TransitionDelay>(buf), &ReactStyle::addProperty<styles::TransitionDelay>);
		c.addFunction(getStyleName<styles::FontFamily>(buf), &ReactStyle::addProperty<styles::FontFamily>);
		c.addFunction(getStyleName<styles::FontSize>(buf), &ReactStyle::addProperty<styles::FontSize>);
		c.addFunction(getStyleName<styles::FontWeight>(buf), &ReactStyle::addProperty<styles::FontWeight>);
		c.addFunction(getStyleName<styles::TextAlign>(buf), &ReactStyle::addProperty<styles::TextAlign>);

		c.endClass();
	}

	void ReactView::reload() {
		if (_lua) {
			lua_close(_lua);
		}

		_root = nullptr;
		removeChildren();

		StyleCache& styleCache = this->getState<StyleCache>();
		styleCache.clear();

		//styleCache.load("E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\react\\test.css");

		_lua = luaL_newstate();
		luaL_openlibs(_lua);

		_root = std::make_shared<ReactRoot>(_lua);
		addChild(_root);

		LuaUtil::reflectStyleComponents(_lua);
		
		luabridge::getGlobalNamespace(_lua)
			.beginNamespace("fw")
				.addFunction("getTime", &getTime)
				.addFunction("loadStyle", [this](const std::string& path) { 
					getState<StyleCache>().load(path);
					_root->updateStyles();
				})
				.beginClass<Color4F>("Color4F")
					.addConstructor<void(), void(f32, f32, f32, f32)>()
					.addProperty("r", &Color4F::r)
					.addProperty("g", &Color4F::g)
					.addProperty("b", &Color4F::b)
					.addProperty("a", &Color4F::a)
				.endClass()
				.beginClass<LengthValue>("LengthValue")
					.addConstructor<void(), void(f32), void(LengthType, f32)>()
					.addProperty("type", &LengthValue::type)
					.addProperty("value", &LengthValue::value)
				.endClass()
				.beginClass<FlexValue>("FlexValue")
					.addConstructor<void(), void(f32), void(FlexUnit, f32), void(const FlexValue&)>()
					.addProperty("unit", &FlexValue::getUnit, &FlexValue::setUnit)
					.addProperty("value", &FlexValue::getValue, &FlexValue::setValue)
				.endClass()
				.beginClass<FlexRect>("FlexRect")
					.addConstructor<
						void(),
						void(const FlexRect&),
						void(FlexValue),
						void(FlexValue, FlexValue, FlexValue, FlexValue),
						void(f32, f32, f32, f32)
					>()
					.addProperty("top", &FlexRect::top)
					.addProperty("left", &FlexRect::left)
					.addProperty("bottom", &FlexRect::bottom)
					.addProperty("right", &FlexRect::right)
				.endClass()
				.beginClass<FlexBorder>("FlexBorder")
					.addConstructor<void(), void(const FlexBorder&), void(f32), void(f32, f32, f32, f32)>()
					.addProperty("top", &FlexBorder::top)
					.addProperty("left", &FlexBorder::left)
					.addProperty("bottom", &FlexBorder::bottom)
					.addProperty("right", &FlexBorder::right)
				.endClass()
				.beginClass<KeyEvent>("KeyEvent")
					.addConstructor<void()>()
					.addProperty("key", &KeyEvent::key)
					.addProperty("action", &KeyEvent::action)
					.addProperty("down", &KeyEvent::down)
				.endClass()
				.beginClass<Object>("Object")
				.endClass()
				.deriveClass<View, Object>("View")
					.addConstructorFrom<std::shared_ptr<View>, void()> ()
					.addFunction("addChild", &View::addChild2)
					.addFunction("removeChild", &View::removeChild2)
					.addFunction("focus", &View::focus)
					.addFunction("unfocus", &View::unfocus)
				.endClass()
				.deriveClass<ReactElementView, View>("ReactElementView")
					.addConstructorFrom<std::shared_ptr<ReactElementView>, void(), void(const std::string&)>()
					.addProperty("id", &ReactElementView::getId, &ReactElementView::setId)
					.addProperty("counterId", &ReactElementView::getCounterId, &ReactElementView::setCounterId)
					.addProperty("className", &ReactElementView::getClassName, &ReactElementView::setClassName)
					.addProperty("tabIndex", &ReactElementView::getTabIndex, &ReactElementView::setTabIndex)
					.addFunction("getStyle", &ReactElementView::getInlineStyle)
					.addFunction("updateLayoutStyle", &ReactElementView::updateLayoutStyle)
				.endClass()
				.deriveClass<ReactRoot, ReactElementView>("ReactRoot")
					.addFunction("addEventListener", &ReactRoot::addEventListener)
					.addFunction("removeEventListener", &ReactRoot::removeEventListener)
				.endClass()
				.deriveClass<ReactTextView, ReactElementView>("ReactTextView")
					.addConstructorFrom<std::shared_ptr<ReactTextView>, void(), void(const std::string&)>()
					.addProperty("nodeValue", &ReactTextView::getNodeValue, &ReactTextView::setNodeValue)
				.endClass()
				.addVariable("document", _root)
			.endNamespace();

		addStyleSetters(_lua);

		int ret = luaL_dofile(_lua, _path.string().c_str());
		if (ret != 0) {
			spdlog::error(lua_tostring(_lua, -1));

			lua_close(_lua);
			_lua = nullptr;
		}
	}

	void ReactView::onInitialize() {
		this->createState<StyleCache>();
		this->createState<IdCounter>();
	}

	void ReactView::onHotReload() {
		reload();
	}
}
