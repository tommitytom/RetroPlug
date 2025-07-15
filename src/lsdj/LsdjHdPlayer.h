#pragma once

#include <vector>

#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "ui/View.h"
#include "core/System.h"
#include "lsdj/LsdjUi.h"
#include "lsdj/LsdjCanvasView.h"
#include "lsdj/LsdjModel.h"
#include "lsdj/LsdjService.h"
#include "ui/SystemOverlayManager.h"
#include "foundation/HashUtil.h"
#include "foundation/StringUtil.h"

namespace rp {
	class Menu;

	namespace SystemUtil {
		SystemServicePtr findService(System& system, SystemServiceType type) {
			for (const auto& service : system.getServices()) {
				if (service->getType() == type) {
					return service;
				}
			}

			return nullptr;
		}
	}

	class LsdjHdPlayer final : public LsdjCanvasView {
		FwRegisterObject();
	private:
		SystemPtr _system;
		lsdj::Ui _ui;

	public:
		LsdjHdPlayer() : LsdjCanvasView({ 160 * 8, 144 * 4 }), _ui(_canvas) {
			setName("LSDJ HD Player");
			setFocusPolicy(fw::FocusPolicy::Click);
			getLayout().setMinDimensions({ 160 * 8, 144 * 4 });
		}

		~LsdjHdPlayer() {}

		void setSystem(SystemPtr& system) {
			_system = system;

			lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
			if (rom.isValid()) {
				_canvas.setFont(rom.getFont(1));
				_canvas.setPalette(rom.getPalette(0));
			}
		}

		SystemPtr getSystem() { return _system; }

		void onInitialize() override {}

		bool onKey(const fw::KeyEvent& ev) override {
			if (ev.key == fw::VirtualKey::Esc && ev.down) {
				this->remove();
				return true;
			}

			if (ev.key == fw::VirtualKey::Space) {
				SystemIoPtr io = _system->getIo(); 
				io->input.buttons.push_back(fw::StreamButtonPress{ .button = (int)fw::ButtonType::Start, .down = ev.down, .duration = 0 });
				return true;
			}

			return false;
		}

		void onUpdate(f32 delta) override {}

		void onRender(fw::Canvas& canvas) override {
			_canvas.clear();

			SystemServicePtr service = SystemUtil::findService(*_system, LSDJ_SERVICE_TYPE);
			if (service) {
				const LsdjServiceSettings& state = service->getStateAs<LsdjServiceSettings>();

				if (state.offsetsValid) {
					MemoryAccessor sramAccessor = _system->getMemory(MemoryType::Sram, AccessType::Read);
					MemoryAccessor ramAccessor = _system->getMemory(MemoryType::Ram, AccessType::Read);

					if (sramAccessor.isValid() && ramAccessor.isValid()) {
						lsdj::Sav sram(sramAccessor.getBuffer());
						lsdj::Ram ram(ramAccessor, state.ramOffsets);

						_ui.renderMode2(sram.getWorkingSong(), ram);
					}
				}
			}

			LsdjCanvasView::onRender(canvas);
		}
	};
}
