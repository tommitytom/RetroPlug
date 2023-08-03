#include "LuaReact.h"

#include "ui/next/Document.h"
#include "ui/next/StyleComponentsMeta.h"

namespace fw {
	f64 getTime() {
		std::chrono::high_resolution_clock::duration time = std::chrono::high_resolution_clock::now().time_since_epoch();
		return std::chrono::duration_cast<std::chrono::duration<f64>>(time).count();
	}
	
	LuaReact::LuaReact(FontManager& fontManager, const std::filesystem::path& path) : _doc(fontManager), _path(path) {
		_listener.setCallback([&](FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
			reload();
		});

		std::string root = std::filesystem::path(_path).remove_filename().string();
		_fileWatcher.addWatch(root, &_listener, true);

		reload();
	}
	
	LuaReact::~LuaReact() {
		if (_lua) {
			lua_close(_lua);
		}
	}

	void LuaReact::update(f32 dt) {
		_fileWatcher.update();

		if (_lua) {
			luabridge::LuaRef cb = luabridge::getGlobal(_lua, "onFrame");
			luabridge::LuaResult result = cb(dt);

			if (!result) {
				spdlog::error(result.errorMessage());
			}
		}
	}

	void LuaReact::reload() {
		if (_lua) {
			lua_close(_lua);
		}

		_doc.clear();

		_lua = luaL_newstate();
		luaL_openlibs(_lua);

		LuaUtil::reflectStyleComponents(_lua);

		luabridge::getGlobalNamespace(_lua)
			.beginNamespace("fw")
				.addFunction("getTime", &getTime)
				.beginClass<Color4F>("Color4F")
					.addConstructor<void(), void(f32, f32, f32, f32)>()
					.addProperty("r", &Color4F::r)
					.addProperty("g", &Color4F::g)
					.addProperty("b", &Color4F::b)
					.addProperty("a", &Color4F::a)
				.endClass()
				.beginClass<FlexValue>("FlexValue")
					.addConstructor<void(), void(f32), void(FlexUnit, f32), void(const FlexValue&)>()
					.addProperty("unit", &FlexValue::getUnit, &FlexValue::setUnit)
					.addProperty("value", &FlexValue::getValue, &FlexValue::setValue)
				.endClass()
				.beginClass<FlexRect>("FlexRect")
					.addConstructor<void(), void(const FlexRect&), void(FlexValue), void(FlexValue, FlexValue, FlexValue, FlexValue)>()
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
				.addVariable("document", &_doc)
			.endNamespace();

		DomStyle rootStyle = _doc.getStyle(_doc.getRootElement());
		rootStyle.setWidth(FlexValue(FlexUnit::Percent, 100.0f));
		rootStyle.setHeight(FlexValue(FlexUnit::Percent, 100.0f));
		rootStyle.setFlexPositionType(FlexPositionType::Absolute);
		rootStyle.setLayoutDirection(LayoutDirection::LTR);
		rootStyle.setFlexDirection(FlexDirection::Row);
		rootStyle.setJustifyContent(FlexJustify::FlexStart);
		rootStyle.setFlexAlignItems(FlexAlign::FlexStart);
		rootStyle.setFlexAlignContent(FlexAlign::Stretch);

		int ret = luaL_dofile(_lua, _path.string().c_str());
		if (ret != 0) {
			spdlog::error(lua_tostring(_lua, -1));
			
			lua_close(_lua);
			_lua = nullptr;
		}
	}
}
