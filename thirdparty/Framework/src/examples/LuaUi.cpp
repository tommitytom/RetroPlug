#include "LuaUi.h"

#include <entt/meta/meta.hpp>
#include <entt/meta/factory.hpp>
#include <entt/meta/resolve.hpp>

using namespace entt::literals;

using namespace fw;

[[nodiscard]] entt::id_type getTypeId(const sol::table& obj) {
	const auto f = obj["__typeId"].get<sol::function>();
	assert(f.valid() && "type_id not exposed to lua!");
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

void LuaUi::reloadScript() {
	_valid = false;
	_renderValid = false;
	_updateValid = false;
	unsubscribeAll();
	removeChildren();

	if (_scriptPath == "") {
		return;
	}

	if (!std::filesystem::exists(_scriptPath)) {
		spdlog::error("Failed to reload script as the script does not exist: {}", _scriptPath);
		return;
	}

	_lua = sol::state();
	_lua.open_libraries(sol::lib::base, sol::lib::package);

	_lua.new_usertype<MouseButtonEvent>("MouseButtonEvent",
		"__typeId", entt::type_index<MouseButtonEvent>::value,
		"button", &MouseButtonEvent::button,
		"down", &MouseButtonEvent::down,
		"position", &MouseButtonEvent::position
	);

	_lua.new_usertype<ButtonClickEvent>("ButtonClickEvent",
		"__typeId", entt::type_index<ButtonClickEvent>::value
	);

	_lua.new_usertype<SliderChangeEvent>("SliderChangeEvent",
		"__typeId", entt::type_index<SliderChangeEvent>::value,
		"value", &SliderChangeEvent::value
	);

	_lua.new_usertype<Rect>("Rect",
		sol::constructors<Rect(), Rect(const Rect&), Rect(int32, int32, int32, int32)>(),
		"x", &Rect::x,
		"y", &Rect::y,
		"w", &Rect::w,
		"h", &Rect::h,
		"right", sol::property(&Rect::right),
		"bottom", sol::property(&Rect::bottom)
	);

	_lua.new_usertype<RectF>("RectF",
		sol::constructors<RectF(), RectF(const RectF&), RectF(f32, f32, f32, f32)>(),
		"x", &RectF::x,
		"y", &RectF::y,
		"w", &RectF::w,
		"h", &RectF::h,
		"right", sol::property(&RectF::right),
		"bottom", sol::property(&RectF::bottom)
	);

	_lua.new_usertype<Color4F>("Color4F",
		sol::constructors<Color4F(), Color4F(const Color4F&), Color4F(f32, f32, f32, f32)>(),
		"r", &Color4F::r,
		"g", &Color4F::g,
		"b", &Color4F::b,
		"a", &Color4F::a
	);

	_lua.new_usertype<Canvas>("Canvas",
		"fillRect", sol::overload(
			entt::overload<Canvas& (const RectF&)>(&Canvas::fillRect),
			entt::overload<Canvas& (const RectF&, const Color4F&)>(&Canvas::fillRect)
		)
	);

	_lua.new_usertype<View>("View",
		"new", sol::factories([]() { return std::make_shared<View>(); }),
		"sizingPolicy", sol::property(&View::getSizingPolicy, &View::setSizingPolicy),
		"name", sol::property(&View::getName, &View::setName),
		"area", sol::property(&View::getArea, &View::setArea),
		"addChild", sol::overload(&View::addChild2),
		"subscribe", [](View& self, const sol::table& ev, ViewPtr source, sol::protected_function f) {
			EventType eventType = getTypeId(ev);
			assert(eventType != -1);
			if (eventType != -1) {
				entt::meta_type t = entt::resolve(eventType);
				assert(t);

				self.subscribe(eventType, source, [t, f](const entt::any& data) {
					if (auto caller = t.func("callSubscriber"_hs); caller) {
						caller.invoke({}, data, f);
					}
				});
			}
		}
	);

	_lua.new_usertype<LabelView>("LabelView",
		sol::base_classes, sol::bases<View>(),
		"new", sol::factories([]() { return std::make_shared<LabelView>(); }),
		"text", sol::property(&LabelView::getText, &LabelView::setText),
		"color", sol::property(&LabelView::getColor, &LabelView::setColor),
		"font", sol::property(&LabelView::getFontFace, &LabelView::setFontFace),
		"fontSize", sol::property(&LabelView::getFontSize, &LabelView::setFontSize)
	);

	_lua.new_usertype<KnobView>("KnobView",
		sol::base_classes, sol::bases<View>(),
		"new", sol::factories([]() { return std::make_shared<KnobView>(); })
	);

	_lua.new_usertype<ButtonView>("ButtonView",
		sol::base_classes, sol::bases<View>(),
		"new", sol::factories([]() { return std::make_shared<ButtonView>(); }),
		"text", sol::property(&ButtonView::getText, &ButtonView::setText)
	);

	_lua.new_usertype<SliderView>("SliderView",
		sol::base_classes, sol::bases<View>(),
		"new", sol::factories([]() { return std::make_shared<SliderView>(); })
	);

	_lua.new_usertype<LuaUi>("LuaUi",
		sol::no_constructor,
		sol::base_classes, sol::bases<View>()
		//"new", sol::factories([]() { return std::make_shared<LuaUi>(); })
	);


	auto result = _lua.script_file(_scriptPath, &sol::script_default_on_error);
	_valid = result.valid();

	if (_valid) {
		_lua["self"] = this;
		_renderValid = true;
		_updateValid = true;

		if (getParent()) {
			onInitialize();
		}
	} else {
		_lua = sol::state();
	}
}
