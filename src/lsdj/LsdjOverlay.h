#pragma once

#include <vector>

#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "ui/View.h"
#include "core/System.h"
#include "lsdj/LsdjCanvasView.h"
#include "lsdj/LsdjModel.h"
#include "ui/SystemOverlayManager.h"
#include "foundation/HashUtil.h"
#include "foundation/StringUtil.h"
#include "ui/SystemOverlay.h"

namespace rp {
	const size_t MAX_UNDO_QUEUE_SIZE = 10;
	const f32 DEFAULT_SONG_SWAP_COOLDOWN = 0.5f;

	class Menu;

	class LsdjOverlay final : public SystemOverlay {
	private:
		SystemPtr _system;
		SystemServicePtr _service;

		fw::Point _mousePosition;

		uint64 _songHash = 0;
		f32 _songSwapCooldown = 0.0f;
		std::vector<fw::Uint8Buffer> _undoQueue;
		size_t _undoPosition = 0;

		bool _aHeld = false;
		bool _bHeld = false;

		LsdjServiceSettings _settings;

	public:
		LsdjOverlay() {
			setType<LsdjOverlay>();
			setName("LSDJ Overlay");
			setSizingPolicy(fw::SizingPolicy::FitToParent);
		}

		~LsdjOverlay() {}

		void onInitialize() override;

		void onMenu(fw::Menu& menu) override;

		bool onKey(const fw::KeyEvent& ev) override;

		bool onDrop(const std::vector<std::string>& paths) override;

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;
	};
}
