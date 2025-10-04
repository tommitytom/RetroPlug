#include "LsdjHdPlayerEcs.h"

#include "ecs/RetroPlugProjectContext.h"

namespace rp {
	constexpr fw::Dimension DIMENSIONS{ 776, 576 };
	constexpr fw::DimensionF DIMENSIONSF{ (f32)DIMENSIONS.w, (f32)DIMENSIONS.h };

	LsdjHdPlayerEcs::LsdjHdPlayerEcs(RetroPlugProject& project, entt::entity system)
		: _project(project),
		_lsdj(project.getRegistry()),
		_canvasView(std::make_shared<LsdjCanvasView>(DIMENSIONS)),
		_ui(_canvasView->getCanvas())
	{
		setName("LSDJ HD Player");
		setFocusPolicy(fw::FocusPolicy::Click);
		getLayout().setMinDimensions({ DIMENSIONSF.w, DIMENSIONSF.h });
		_canvasView->getLayout().setMinDimensions({ DIMENSIONSF.w, DIMENSIONSF.h });

		setSystem(system);
	}

	LsdjHdPlayerEcs::~LsdjHdPlayerEcs() {
		if (_system != entt::null && _project.getRegistry().valid(_system)) {
			_project.unsubscribeFromMemory(_system, MemoryType::Ram);
		}
	}

	void LsdjHdPlayerEcs::onInitialize() {
		this->addChild(_canvasView);
	}

	void LsdjHdPlayerEcs::setSystem(entt::entity system) {
		if (_system != entt::null && _project.getRegistry().valid(_system)) {
			_project.unsubscribeFromMemory(_system, MemoryType::Ram);
		}

		_system = system;

		lsdj::Rom rom = _lsdj.getLsdjRom(_system);
		lsdj::Song song = _lsdj.getLsdjWorkingSong(_system);
		if (rom.isValid() && song.isValid()) {
			uint8 fontIndex = (song.getFontIndex() + 1) % 3;
			uint8 paletteIndex = song.getPaletteIndex();
			_canvasView->getCanvas().setFont(rom.getFont(fontIndex));
			_canvasView->getCanvas().setPalette(rom.getPalette(paletteIndex));
		}

		_project.subscribeToMemory(_system, MemoryType::Ram);
	}

	bool LsdjHdPlayerEcs::onKey(const fw::KeyEvent& ev) {
		if (ev.key == fw::VirtualKey::Esc && ev.down) {
			this->remove();
			return true;
		}

		const InputConfig& inputConfig = _project.getInputConfig();
		auto found = inputConfig.keyboard.find(ev.key);
		if (found != inputConfig.keyboard.end()) {
			_project.getEventNode().trySend("Audio"_hs, PadButtonEvent{
				.entity = _system,
				.button = found->second,
				.down = ev.down
			});

			return true;
		}

		return false;
	}

	void LsdjHdPlayerEcs::onRender(fw::Canvas& canvas) {
		_canvasView->getCanvas().clear();

		lsdj::Song song = _lsdj.getLsdjWorkingSong(_system);
		lsdj::Rom rom = _lsdj.getLsdjRom(_system);
		lsdj::Ram ram = _lsdj.getLsdjRam(_system);

		if (song.isValid() && rom.isValid() && ram.isValid()) {
			_ui.renderMode2(rom, song, ram);
		}

		_canvasView->setAlpha(1);
	}
}
