#include "LsdjOverlay.h"

#include "core/Project.h"
#include "ui/MenuView.h"
#include "ui/SamplerView.h"
#include "ui/SystemView.h"

using namespace rp;

void showSampleManager(View* parent, SystemWrapperPtr system, LsdjModel& state) {
	std::vector<SamplerView*> samplers;
	parent->findChildren<SamplerView>(samplers);

	for (SamplerView* sampler : samplers) {
		if (sampler->getSystem() == system) {
			// Already open - focus and return
			sampler->focus();
			return;
		}
	}

	std::shared_ptr<SamplerView> view = parent->addChild<SamplerView>("LSDj Sample Manager");
	view->setSystem(system);

	view->focus();
}

void LsdjOverlay::onInitialized() {
	_system = getParent()->asRaw<SystemView>()->getSystem();

	Project* project = getShared<Project>();
	_model = _system->getModel<LsdjModel>();
	_system->reset();

	SystemPtr system = _system->getSystem();
	MemoryAccessor buffer = system->getMemory(MemoryType::Rom, AccessType::Read);

	if (buffer.isValid()) {
		lsdj::Rom rom(buffer);

		_canvas.setFont(rom.getFont(1));
		_canvas.setPalette(rom.getPalette(0));

		_offsetsValid = lsdj::OffsetLookup::findOffsets(buffer.getBuffer(), _ramOffsets, false);

		if (_offsetsValid) {
			_refresher.setSystem(system, _ramOffsets);
		} else {
			spdlog::warn("Failed to find ROM offsets");
		}
	}
}

void LsdjOverlay::onMenu(Menu& menu) {
	menu.subMenu("LSDJ")
		.action("Sample Manager", [this]() { showSampleManager(getParent()->getParent(), _system, *_model); })
		.parent();
}

bool LsdjOverlay::onKey(VirtualKey::Enum key, bool down) {
	SystemPtr system = _system->getSystem();

	if (down && key == VirtualKey::Z) {
		if (_undoPosition > 1) {
			_undoPosition--;

			spdlog::info("UNDO");

			// Copy frame buffer and display it until refresh is finished

			lsdj::Ram ram(system->getMemory(MemoryType::Ram, AccessType::Read), _ramOffsets);
			MemoryAccessor sram = system->getMemory(MemoryType::Sram, AccessType::Write);

			if (ram.isValid() && sram.isValid()) {
				_songHash = HashUtil::hash(_undoQueue[_undoPosition]);
				_songSwapCooldown = DEFAULT_SONG_SWAP_COOLDOWN;
				sram.write(0, _undoQueue[_undoPosition]);

				_refresher.refresh();
			}
		}

		return true;
	}

	return false;
}

bool LsdjOverlay::onMouseMove(Point<uint32> pos) {
	_mousePosition = pos;
	SystemPtr system = _system->getSystem();

	Point<uint8> cursorPos;
	if (LsdjUtil::pixelToCursorPos(pos, cursorPos)) {
		lsdj::Ram ram(system->getMemory(MemoryType::Ram, AccessType::Read), _ramOffsets);

		if (ram.isValid()) {
			//ram.setCursorPosition(cursorPos);
			//ram.setScreen(lsdj::ScreenType::Chain);
		}

		return true;
	}

	return false;
}

void LsdjOverlay::onUpdate(f32 delta) {
	if (_system) {
		SystemPtr system = _system->getSystem();

		if (_songSwapCooldown > 0.0f) {
			_songSwapCooldown -= delta;
		}

		_refresher.update(delta);

		MemoryAccessor buffer = system->getMemory(MemoryType::Sram, AccessType::Read);
		if (buffer.isValid()) {
			Uint8Buffer songBuffer = buffer.getBuffer().slice(0, LSDJ_SONG_BYTE_COUNT);
			uint64 songHash = HashUtil::hash(songBuffer);

			if (songHash != _songHash && _songSwapCooldown <= 0.0f) {
				//spdlog::info("SRAM Changed!");

				if (_undoQueue.size() > 0 && _undoPosition < _undoQueue.size() - 1) {
					_undoQueue.resize(_undoPosition + 1);
				}

				if (_undoQueue.size() == MAX_UNDO_QUEUE_SIZE) {
					_undoQueue.erase(_undoQueue.begin());
				}

				_undoPosition = _undoQueue.size();
				_undoQueue.push_back(songBuffer.clone());

				_songHash = songHash;
			}
		}
	}
}

void LsdjOverlay::onRender() {
	_canvas.clear();
	//_canvas.text(0, 0, "SHIT", lsdj::ColorSets::Normal);

	if (_refresher.getOverlay()) {
		Image& target = _canvas.getRenderTarget();
		_refresher.getOverlay()->getBuffer().copyTo(&target.getBuffer());
	}

	LsdjCanvasView::onRender();
}
