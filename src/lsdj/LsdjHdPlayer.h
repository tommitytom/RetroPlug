#pragma once

#include <vector>

#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "ui/View.h"
#include "core/System.h"
#include "lsdj/LsdjUi.h"
#include "lsdj/LsdjCanvasView.h"
#include "lsdj/LsdjModel.h"
#include "ui/SystemOverlayManager.h"
#include "foundation/HashUtil.h"
#include "foundation/StringUtil.h"

namespace rp {
	class Menu;

	class LsdjHdPlayer final : public LsdjCanvasView {
	private:
		SystemPtr _system;
		lsdj::Ui _ui;

	public:
		LsdjHdPlayer() : LsdjCanvasView({ 160 * 5, 144 * 4 }), _ui(_canvas) {
			setType<LsdjHdPlayer>();
			setName("LSDJ HD Player");
			setSizingPolicy(fw::SizingPolicy::None);
			setFocusPolicy(fw::FocusPolicy::Click);
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
			if (ev.key == VirtualKey::Esc && ev.down) {
				this->remove();
				return true;
			}

			return false;
		}

		void onUpdate(f32 delta) override {}

		void onRender(fw::Canvas& canvas) override {
			_canvas.clear();

			/*LsdjModelPtr model = _system->getModel<LsdjModel>();
			if (model && model->getOffsetsValid()) {
				MemoryAccessor sramAccessor = model->getSystem()->getMemory(MemoryType::Sram, AccessType::Read);
				MemoryAccessor ramAccessor = model->getSystem()->getMemory(MemoryType::Ram, AccessType::Read);

				if (sramAccessor.isValid() && ramAccessor.isValid()) {
					lsdj::Sav sram(sramAccessor.getBuffer());
					lsdj::Ram ram(ramAccessor, model->getMemoryOffsets());

					_ui.renderMode2(sram.getWorkingSong(), ram);
				}
			}*/

			LsdjCanvasView::onRender(canvas);
		}
	};
}
