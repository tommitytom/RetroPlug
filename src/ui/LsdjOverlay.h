#pragma once

#include <vector>

#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "View.h"
#include "core/SystemWrapper.h"
#include "ui/LsdjCanvasView.h"
#include "ui/LsdjModel.h"
#include "ui/SystemOverlayManager.h"
#include "util/HashUtil.h"
#include "foundation/StringUtil.h"

namespace rp {
	const size_t MAX_UNDO_QUEUE_SIZE = 10;
	const f32 DEFAULT_SONG_SWAP_COOLDOWN = 0.5f;

	class Menu;

	class LsdjOverlay final : public LsdjCanvasView {
	private:
		SystemWrapperPtr _system;
		LsdjModelPtr _model;

		Point _mousePosition;

		uint64 _songHash = 0;
		f32 _songSwapCooldown = 0.0f;
		std::vector<Uint8Buffer> _undoQueue;
		size_t _undoPosition = 0;

		bool _aHeld = false;
		bool _bHeld = false;

	public:
		LsdjOverlay(): LsdjCanvasView({ 160, 144 }) {
			setType<LsdjOverlay>();
			setName("LSDJ Overlay");
			setSizingPolicy(SizingPolicy::FitToParent);
		}

		~LsdjOverlay() {}

		void onInitialize() override;

		void onMenu(Menu& menu) override;

		bool onKey(VirtualKey::Enum key, bool down) override;

		bool onDrop(const std::vector<std::string>& paths) override;

		void onUpdate(f32 delta) override;

		void onRender(Canvas& canvas) override;
	};
}
