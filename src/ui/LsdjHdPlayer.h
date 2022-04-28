#pragma once

#include <vector>

#include <nanovg.h>
#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "View.h"
#include "core/SystemWrapper.h"
#include "lsdj/LsdjUi.h"
#include "ui/LsdjCanvasView.h"
#include "ui/LsdjModel.h"
#include "ui/SystemOverlayManager.h"
#include "util/HashUtil.h"
#include "util/StringUtil.h"

namespace rp {
	class Menu;

	class LsdjHdPlayer final : public LsdjCanvasView {
	private:
		SystemWrapperPtr _system;
		lsdj::Ui _ui;

	public:
		LsdjHdPlayer() : LsdjCanvasView({ 160 * 4, 144 * 4 }), _ui(_canvas) {
			setType<LsdjHdPlayer>();
			setName("LSDJ HD Player");
			setSizingMode(SizingMode::None);
		}

		~LsdjHdPlayer() {}

		void setSystem(SystemWrapperPtr& system) {
			_system = system;

			lsdj::Rom rom = system->getSystem()->getMemory(MemoryType::Rom, AccessType::Read);
			if (rom.isValid()) {
				_canvas.setFont(rom.getFont(1));
				_canvas.setPalette(rom.getPalette(0));
			}
		}

		SystemWrapperPtr getSystem() { return _system; }

		void onInitialized() override {}

		bool onKey(VirtualKey::Enum key, bool down) override {
			if (key == VirtualKey::Esc && down) {
				this->remove();
				return true;
			}

			return false;
		}

		void onUpdate(f32 delta) override {}

		void onRender() override {
			_canvas.clear();

			LsdjModelPtr model = _system->getModel<LsdjModel>();
			if (model && model->getOffsetsValid()) {
				MemoryAccessor sramAccessor = model->getSystem()->getMemory(MemoryType::Sram, AccessType::Read);
				MemoryAccessor ramAccessor = model->getSystem()->getMemory(MemoryType::Ram, AccessType::Read);

				if (sramAccessor.isValid() && ramAccessor.isValid()) {
					lsdj::Sav sram(sramAccessor.getBuffer());
					lsdj::Ram ram(ramAccessor, model->getMemoryOffsets());

					_ui.renderMode2(sram.getWorkingSong(), ram);
				}
			}
			
			LsdjCanvasView::onRender();
		}
	};
}
