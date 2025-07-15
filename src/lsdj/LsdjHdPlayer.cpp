#include "LsdjHdPlayer.h"

namespace rp {
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

	LsdjHdPlayer::LsdjHdPlayer() : _canvasView(std::make_shared<LsdjCanvasView>(fw::Dimension{ 160 * 4, 144 * 4 })), _ui(_canvasView->getCanvas()) {
		setName("LSDJ HD Player");
		setFocusPolicy(fw::FocusPolicy::Click);
		getLayout().setMinDimensions({ 160 * 4, 144 * 4 });
		_canvasView->getLayout().setMinDimensions({ 160 * 4, 144 * 4 });
	}

	void LsdjHdPlayer::onInitialize() {
		this->addChild(_canvasView);
	}

	void LsdjHdPlayer::setSystem(const SystemPtr& system) {
		_system = system;

		MemoryAccessor sramAccessor = _system->getMemory(MemoryType::Sram, AccessType::Read);
		lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
		if (rom.isValid() && sramAccessor.isValid()) {
			lsdj::Sav sram(sramAccessor.getBuffer());
			uint8 fontIndex = (sram.getWorkingSong().getFontIndex() + 1) % 3;
			uint8 paletteIndex = sram.getWorkingSong().getPaletteIndex();
			_canvasView->getCanvas().setFont(rom.getFont(fontIndex));
			_canvasView->getCanvas().setPalette(rom.getPalette(paletteIndex));
		}
	}

	void LsdjHdPlayer::processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) {
		_system->processInput(buttons, actions);
	}

	bool LsdjHdPlayer::onKey(const fw::KeyEvent& ev) {
		if (ev.key == fw::VirtualKey::Esc && ev.down) {
			this->remove();
			return true;
		}

		return false;
	}

	void LsdjHdPlayer::onRender(fw::Canvas& canvas) {
		_canvasView->getCanvas().clear();

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

		_canvasView->setAlpha(1);
	}
}
