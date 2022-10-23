#pragma once

#include <spdlog/spdlog.h>
#include <stb/stb_image_write.h>

#include "core/System.h"
#include "lsdj/Ram.h"

namespace rp {
	enum class RefreshState {
		None,
		Screen1,
		Screen2
	};

	class LsdjRefresher {
	private:
		SystemPtr _system;
		fw::ImagePtr _lastFrame;
		fw::ImagePtr _overlay;

		lsdj::MemoryOffsets _offsets;

		RefreshState _refreshState = RefreshState::None;
		uint8 _screen1 = 0;
		uint8 _screen2 = 0;

		bool _chainRefresh = false;

	public:
		LsdjRefresher() {}
		~LsdjRefresher() {}

		void update(f32 delta);

		void refresh();

		void setSystem(SystemPtr system, const lsdj::MemoryOffsets& offsets) {
			_system = system;
			_offsets = offsets;
		}

		bool isValid() const {
			return _system && _offsets.cursorX != 0;
		}

		fw::ImagePtr getOverlay() {
			return _overlay;
		}
	};
}
