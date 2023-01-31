#include "LuaUi.h"

#include <entt/meta/meta.hpp>
#include <entt/meta/factory.hpp>
#include <entt/meta/resolve.hpp>
#include <sol/sol.hpp>

#include "ui/KnobView.h"
#include "ui/LabelView.h"
#include "ui/PanelView.h"
#include "ui/ButtonView.h"
#include "ui/SliderView.h"
#include "ui/TextureView.h"

using namespace entt::literals;
using namespace fw;

SOL_BASE_CLASSES(fw::ButtonView, fw::View);
SOL_BASE_CLASSES(fw::KnobView, fw::View);
SOL_BASE_CLASSES(fw::LabelView, fw::View);
SOL_BASE_CLASSES(fw::LuaUi, fw::View);
SOL_BASE_CLASSES(fw::SliderView, fw::View);
SOL_BASE_CLASSES(fw::PanelView, fw::View);
SOL_BASE_CLASSES(fw::TextureView, fw::View);
SOL_DERIVED_CLASSES(fw::View, fw::ButtonView, fw::LabelView, fw::KnobView, fw::LuaUi, fw::SliderView, fw::PanelView, fw::TextureView);

[[nodiscard]] entt::id_type getTypeId(const sol::table& obj) {
	const auto f = obj["__typeId"].get<sol::function>();
	return f.valid() ? f().get<entt::id_type>() : -1;
}

template <typename EventT>
auto callSubscriber(const entt::any& data, const sol::function& f) {
	const EventT& ev = entt::any_cast<const EventT&>(data);
	f(ev);
}

template <typename EventT> 
void registerEvent() {
	entt::meta<EventT>()
		.type(entt::type_id<EventT>().index())
		.template func<&callSubscriber<EventT>>("callSubscriber"_hs);
}

inline sol::protected_function_result scriptErrorHandler(lua_State* L, sol::protected_function_result pfr) {
	sol::error err = pfr;
	spdlog::error(err.what());
	return pfr;
}

template <typename Class, typename... Args>
sol::usertype<Class> new_usertype(sol::state& lua, const std::string& name, Args&&... args) {
	return lua.new_usertype<Class>(name,
		//"__typeId", entt::type_index<Class>::value,
		std::forward<Args>(args)...
	);
}

template <typename T>
auto createViewFactory(sol::state* lua) {
	return sol::factories([]() {
		return std::make_shared<T>();
	}, [lua](const sol::table& items) {
		auto view = std::make_shared<T>();
		sol::protected_function init = lua->get<sol::protected_function>("updateProps");
		sol::protected_function_result res = init(view, items);

		if (!res.valid()) {
			sol::error err = res;
			spdlog::error(err.what());
		}

		return view;
	});
}

LuaUi::LuaUi() : View({ 1024, 768 }) {
	setType<LuaUi>();
	setSizingPolicy(SizingPolicy::FitToParent);
	setFocusPolicy(FocusPolicy::Click);

	registerEvent<MouseButtonEvent>();
	registerEvent<ButtonClickEvent>();
	registerEvent<SliderChangeEvent>();

	_listener.setFunc([&](FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
		this->reloadScript();
	});

	auto examplesDir = std::filesystem::current_path().parent_path().parent_path() / "src" / "examples";
	spdlog::info(examplesDir.string());

	setScriptPath(examplesDir / "LuaUi.lua");
}

LuaUi::~LuaUi() {
	unsubscribeAll();
	removeChildren();
	delete _lua;
}

#include "foundation/SolUtil.h"

void LuaUi::reloadScript() {
	if (_scriptPath == "") {
		return;
	}

	if (!std::filesystem::exists(_scriptPath)) {
		spdlog::error("Failed to reload script as the script does not exist: {}", _scriptPath);
		return;
	}

	sol::state* lua = new sol::state();
	lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::table);

	SolUtil::addIncludePath(*lua, std::filesystem::path(_scriptPath).parent_path().string());

	lua->new_enum("SizingPolicy",
		"None", SizingPolicy::None,
		"FitToContent", SizingPolicy::FitToContent,
		"FitToParent", SizingPolicy::FitToParent
	);

	lua->new_usertype<MouseButtonEvent>("MouseButtonEvent",
		"__typeId", entt::type_index<MouseButtonEvent>::value,
		"button", &MouseButtonEvent::button,
		"down", &MouseButtonEvent::down,
		"position", &MouseButtonEvent::position
	);

	lua->new_usertype<ButtonClickEvent>("ButtonClickEvent",
		"__typeId", entt::type_index<ButtonClickEvent>::value
	);

	lua->new_usertype<SliderChangeEvent>("SliderChangeEvent",
		"__typeId", entt::type_index<SliderChangeEvent>::value,
		"value", &SliderChangeEvent::value
	);

	lua->new_usertype<Rect>("Rect",
		sol::constructors<Rect(), Rect(const Rect&), Rect(int32, int32, int32, int32)>(),
		"x", &Rect::x,
		"y", &Rect::y,
		"w", &Rect::w,
		"h", &Rect::h,
		"right", sol::property(&Rect::right),
		"bottom", sol::property(&Rect::bottom)
	);

	lua->new_usertype<RectF>("RectF",
		sol::constructors<RectF(), RectF(const RectF&), RectF(f32, f32, f32, f32)>(),
		"x", &RectF::x,
		"y", &RectF::y,
		"w", &RectF::w,
		"h", &RectF::h,
		"right", sol::property(&RectF::right),
		"bottom", sol::property(&RectF::bottom)
	);

	lua->new_usertype<Color4F>("Color4F",
		sol::constructors<Color4F(), Color4F(const Color4F&), Color4F(f32, f32, f32, f32)>(),
		"r", &Color4F::r,
		"g", &Color4F::g,
		"b", &Color4F::b,
		"a", &Color4F::a
	);

	lua->new_usertype<Canvas>("Canvas",
		sol::no_constructor,
		"fillRect", sol::overload(
			entt::overload<Canvas& (const RectF&)>(&Canvas::fillRect),
			entt::overload<Canvas& (const RectF&, const Color4F&)>(&Canvas::fillRect)
		)
	);

	lua->new_usertype<View>("View",
		"new", createViewFactory<View>(lua),
		"sizingPolicy", sol::property(&View::getSizingPolicy, &View::setSizingPolicy),
		"name", sol::property(&View::getName, &View::setName),
		"area", sol::property(&View::getArea, &View::setArea),
		"addChild", sol::overload(&View::addChild2),
		"subscribe", [](View& self, const sol::table& ev, ViewPtr source, sol::protected_function f) {
			EventType eventType = getTypeId(ev);
			if (eventType == -1) {
				spdlog::error("Unknown event type - make sure the object has a __typeId field");
				return;
			}

			entt::meta_type t = entt::resolve(eventType);
			if (!t) {
				spdlog::error("Unknown event type - make sure the event has been registered with registerEvent<T>()");
				return;
			}

			entt::meta_func subFunc = t.func("callSubscriber"_hs);
			if (!subFunc) {
				spdlog::error("Failed to find subscription handler - make sure the event has been registered with registerEvent<T>()");
				return;
			}

			self.subscribe(eventType, source, [subFunc, f](const entt::any& data) {
				subFunc.invoke({}, data, f);
			});
		}
	);

	auto ut = lua->new_usertype<LabelView>("LabelView",
		sol::base_classes, sol::bases<View>(),
		"new", createViewFactory<LabelView>(lua),
		"text", sol::property(&LabelView::getText, &LabelView::setText),
		"color", sol::property(&LabelView::getColor, &LabelView::setColor),
		"font", sol::property(&LabelView::getFontFace, &LabelView::setFontFace),
		"fontSize", sol::property(&LabelView::getFontSize, &LabelView::setFontSize)
	);

	lua->new_usertype<KnobView>("KnobView",
		sol::base_classes, sol::bases<View>(),
		"new", createViewFactory<KnobView>(lua)
	);

	lua->new_usertype<ButtonView>("ButtonView",
		sol::base_classes, sol::bases<View>(),
		"new", createViewFactory<ButtonView>(lua),
		"text", sol::property(&ButtonView::getText, &ButtonView::setText)
	);

	lua->new_usertype<SliderView>("SliderView",
		sol::base_classes, sol::bases<View>(),
		"new", createViewFactory<SliderView>(lua)
	);

	lua->new_usertype<PanelView>("PanelView",
		sol::base_classes, sol::bases<View>(),
		"new", createViewFactory<PanelView>(lua),
		"color", sol::property(&PanelView::getColor, &PanelView::setColor)
	);

	lua->new_usertype<TextureView>("TextureView",
		sol::base_classes, sol::bases<View>(),
		"new", createViewFactory<TextureView>(lua),
		"uri", sol::property(&TextureView::getUri, &TextureView::setUri)
	);

	spdlog::info(std::filesystem::current_path().string());

	lua->new_usertype<LuaUi>("LuaUi",
		sol::no_constructor,
		sol::base_classes, sol::bases<View>()
	);

	sol::protected_function_result result = lua->safe_script_file(_scriptPath, &scriptErrorHandler);
	
	if (result.valid()) {
		unsubscribeAll();
		removeChildren();
		delete _lua;
		_lua = lua;
		
		lua->set("uiSelf", this);
		_renderValid = true;
		_updateValid = true;

		if (getParent()) {
			onInitialize();
		}
	} else {
		result = sol::protected_function_result();
		delete lua;
	}
}

void LuaUi::onInitialize() {
	if (_lua) {
		sol::protected_function f = _lua->get<sol::protected_function>("onInitialize");
		if (f) {
			sol::protected_function_result res = f();

			if (!res.valid()) {
				sol::error err = res;
				spdlog::error(err.what());
			}
		}
	}
}

bool LuaUi::onMouseButton(const MouseButtonEvent& ev) {
	if (_lua) {
		sol::protected_function f = _lua->get<sol::protected_function>("onMouseButton");
		if (f) {
			sol::protected_function_result res = f(ev);

			if (!res.valid()) {
				sol::error err = res;
				spdlog::error(err.what());
			} else {
				if (res.return_count() > 0) {
					bool handled = res;
					return handled;
				}

				return true;
			}
		}
	}

	return false;
}

bool LuaUi::onKey(const KeyEvent& ev) {
	if (_lua) {
		sol::protected_function f = _lua->get<sol::protected_function>("onKey");
		if (f) {
			sol::protected_function_result res = f(ev);

			if (!res.valid()) {
				sol::error err = res;
				spdlog::error(err.what());
			} else {
				if (res.return_count() > 0) {
					bool handled = res;
					return handled;
				}

				return true;
			}
		}
	}

	return false;
}

void LuaUi::onUpdate(f32 delta) {
	_watcher.update();

	if (_updateValid) {
		sol::protected_function f = _lua->get<sol::protected_function>("onUpdate");
		if (f) {
			sol::protected_function_result res = f(delta);

			if (!res.valid()) {
				sol::error err = res;
				spdlog::error(err.what());
				_updateValid = false;
			}
		}
	}
}

void LuaUi::onRender(Canvas& canvas) {
	if (_renderValid) {
		sol::protected_function f = _lua->get<sol::protected_function>("onRender");
		if (f) {
			sol::protected_function_result res = f(canvas);

			if (!res.valid()) {
				sol::error err = res;
				spdlog::error(err.what());
				_renderValid = false;
			}
		}
	}
}
