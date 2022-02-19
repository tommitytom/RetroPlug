#pragma once

#include <vector>

#include <nanovg.h>
#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "View.h"
#include "core/SystemWrapper.h"
#include "ui/LsdjCanvasView.h"
#include "ui/LsdjModel.h"
#include "ui/SystemOverlayManager.h"
#include "util/HashUtil.h"
#include "util/StringUtil.h"

namespace rp {
	const size_t MAX_UNDO_QUEUE_SIZE = 10;
	const f32 DEFAULT_SONG_SWAP_COOLDOWN = 0.5f;

	class Menu;

	class LsdjOverlay final : public LsdjCanvasView {
	private:
		SystemWrapperPtr _system;
		LsdjModelPtr _model;

		Point<uint32> _mousePosition;

		uint64 _songHash = 0;
		f32 _songSwapCooldown = 0.0f;
		std::vector<Uint8Buffer> _undoQueue;
		size_t _undoPosition = 0;

	public:
		LsdjOverlay(): LsdjCanvasView({ 160, 144 }) {
			setType<LsdjOverlay>();
			setName("LSDJ Overlay");
			setSizingMode(SizingMode::FitToParent);
		}

		~LsdjOverlay() {}

		void onInitialized() override;

		void onMenu(Menu& menu) override;

		bool onKey(VirtualKey::Enum key, bool down) override;

		bool onDrop(const std::vector<std::string>& paths) override;

		void onUpdate(f32 delta) override;

		void onRender() override;
	};
}
