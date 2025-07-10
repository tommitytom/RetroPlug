#pragma once

#include <sol/sol.hpp>
#include "foundation/Input.h"
#include "foundation/SolUtil.h"
#include "foundation/ButtonStream.h"

namespace rp {
	class InputMapper {
	private:
		sol::state _lua;
		bool _valid = false;

	public:
		InputMapper(std::string_view content) {
			fw::SolUtil::prepareState(_lua);
			
			const sol::load_result result = _lua.load_buffer(content.data(), content.size(), sol::detail::default_chunk_name(), sol::load_mode::text);
			_valid = result.valid();
		}

		bool processKey(fw::VirtualKey key, bool down) {
			std::vector<StreamButtonPress> buttons;
			sol::function func = _lua["processKey"];
			func(key, down, buttons);
		}

		bool isValid() const {
			return _valid;
		}
	};
}
