#pragma once

#include <vector>

#include <nanovg.h>
#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "View.h"
#include "core/System.h"
#include "lsdj/Ram.h"
#include "lsdj/OffsetLookup.h"
#include "lsdj/LsdjCanvas.h"
#include "lsdj/LsdjUtil.h"
#include "ui/LsdjCanvasView.h"
#include "util/HashUtil.h"
#include "ui/LsdjRefresher.h"
#include "ui/LsdjModel.h"
#include "util/StringUtil.h"
#include "ui/SystemOverlayManager.h"

namespace rp {
	const size_t MAX_UNDO_QUEUE_SIZE = 10;
	const f32 DEFAULT_SONG_SWAP_COOLDOWN = 0.5f;

	class Menu;

	class LsdjOverlay final : public LsdjCanvasView {
	private:
		SystemPtr _system;
		std::shared_ptr<LsdjModel> _model;
		
		lsdj::MemoryOffsets _ramOffsets;
		bool _offsetsValid = false;

		Point<uint32> _mousePosition;

		uint64 _songHash = 0;
		f32 _songSwapCooldown = 0.0f;
		std::vector<Uint8Buffer> _undoQueue;
		size_t _undoPosition = 0;

		LsdjRefresher _refresher;

	public:
		LsdjOverlay(): LsdjCanvasView({ 160, 144 }) {
			setType<LsdjOverlay>(); 
			setName("LSDJ Overlay");
			setSizingMode(SizingMode::FitToParent);
		}

		~LsdjOverlay() {}

		void onInitialized() final override;

		void onMenu(Menu& menu) final override;

		bool onKey(VirtualKey::Enum key, bool down) final override;

		bool onMouseMove(Point<uint32> pos) final override;

		void onUpdate(f32 delta) final override;

		void onRender() final override;
	};
}
