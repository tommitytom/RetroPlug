#include "LsdjOverlay.h"

#include "core/FileManager.h"
#include "core/Project.h"
#include "ui/LsdjHdPlayer.h"
#include "ui/MenuView.h"
#include "ui/SamplerView.h"
#include "ui/SystemView.h"
#include "ui/Menu.h"

using namespace rp;

std::shared_ptr<SamplerView> showSampleManager(fw::View* parent, SystemWrapperPtr system) {
	std::vector<std::shared_ptr<SamplerView>> samplers;
	parent->findChildren<SamplerView>(samplers);

	std::shared_ptr<SamplerView> samplerView;

	for (std::shared_ptr<SamplerView> sampler : samplers) {
		if (sampler->getSystem() == system) {
			// Already open - focus and return
			samplerView = sampler;
		}
	}

	if (!samplerView) {
		samplerView = parent->addChild<SamplerView>("LSDj Sample Manager");
		samplerView->setSystem(system);

		// TODO: Show kit currently under cursor!
		//samplerView->setSampleIndex()
	}

	samplerView->focus();

	return samplerView;
}

void showHdPlayer(fw::View* parent, SystemWrapperPtr system) {
	auto player = parent->addChild<LsdjHdPlayer>("LSDJ HD Player");
	player->setSystem(system);
	player->focus();
}

void LsdjOverlay::onInitialize() {
	_system = getParent()->asRaw<SystemView>()->getSystem();

	Project* project = getShared<Project>();
	_model = _system->getModel<LsdjModel>();

	if (_model->isRomValid()) {
		SystemPtr system = _system->getSystem();
		MemoryAccessor buffer = system->getMemory(MemoryType::Rom, AccessType::Read);

		if (buffer.isValid()) {
			lsdj::Rom rom(buffer);
			_canvas.setFont(rom.getFont(1));
			_canvas.setPalette(rom.getPalette(0));
		}
	}
}

void LsdjOverlay::onMenu(fw::Menu& menu) {
	menu.subMenu("LSDJ")
		.action("Sample Manager", [this]() { showSampleManager(getParent()->getParent(), _system); })
		.action("HD Player", [this]() { showHdPlayer(getParent()->getParent()->getParent(), _system); })
		.parent();
}

bool LsdjOverlay::onKey(VirtualKey::Enum key, bool down) {
	if (key == VirtualKey::Tab) {
		// TODO: This is temporary.  Ideally there will be a global key handler that picks up tabs for moving between instances etc!
		return false;
	}

	SystemPtr system = _system->getSystem();
	LsdjModelPtr model = _system->getModel<LsdjModel>();

	bool changed = false;
	if (key == VirtualKey::W) {
		_bHeld = down;
		changed = true;
	}

	if (key == VirtualKey::D) {
		_aHeld = down;
		changed = true;
	}

	if (changed && (!_aHeld && !_bHeld) && model->isSramDirty()) {
		_system->saveSram();
	}

	/*LsdjModelPtr model = _system->getModel<LsdjModel>();
	if (model->getOffsetsValid() && down && key == VirtualKey::Z) {
		if (_undoPosition > 1) {
			_undoPosition--;

			spdlog::info("UNDO");

			// Copy frame buffer and display it until refresh is finished

			lsdj::Ram ram(system->getMemory(MemoryType::Ram, AccessType::Read), model->getMemoryOffsets());
			MemoryAccessor sram = system->getMemory(MemoryType::Sram, AccessType::Write);

			if (ram.isValid() && sram.isValid()) {
				_songHash = HashUtil::hash(_undoQueue[_undoPosition]);
				_songSwapCooldown = DEFAULT_SONG_SWAP_COOLDOWN;
				sram.write(0, _undoQueue[_undoPosition]);

				//_refresher.refresh();
			}
		}

		return true;
	}*/

	return false;
}

bool LsdjOverlay::onDrop(const std::vector<std::string>& paths) {
	SystemPtr system = _system->getSystem();
	LsdjModelPtr model = _system->getModel<LsdjModel>();

	if (!model->isRomValid()) {
		return false;
	}

	//bool foundSamples = false;
	size_t kitIdx = -1;

	std::vector<std::string> samples;

	for (const std::string& path : paths) {
		if (fs::is_directory(path)) {
			std::vector<std::string> dirSamples;

			for (const fs::directory_entry& item : fs::directory_iterator(path)) {
				if (item.path().extension() == ".wav") {
					dirSamples.push_back(item.path().string());
					//foundSamples = true;
				} else if (item.path().extension() == ".kit") {
					kitIdx = model->addKit(system, item.path().string());
					//foundSamples = true;
				}
			}

			kitIdx = model->addKitSamples(system, dirSamples);
		} else if (fs::path(path).extension() == ".wav") {
			samples.push_back(path);
			//foundSamples = true;
		} else if (fs::path(path).extension() == ".kit") {
			kitIdx = model->addKit(system, path);
			//foundSamples = true;
		}
	}

	if (samples.size() > 0) {
		FileManager* fileManager = getState<FileManager>();
		std::string kitName;

#ifndef RP_WEB
		kitName = fw::FsUtil::getDirectoryName(samples[0]);
#endif

		// Make a local copy of the sample if we don't already have it
		for (std::string& samplePath : samples) {
			samplePath = fileManager->addHashedFile(samplePath, "samples").string();
		}

		kitIdx = model->addKitSamples(system, samples, kitName);
	}

	if (kitIdx != -1) {
		system->reset();
		auto samplerView = showSampleManager(getParent()->getParent(), _system);
		samplerView->setSampleIndex(kitIdx, 0);

		return true;
	}

	return false;
}

void LsdjOverlay::onUpdate(f32 delta) {
	/*if (_system) {
		SystemPtr system = _system->getSystem();

		if (_songSwapCooldown > 0.0f) {
			_songSwapCooldown -= delta;
		}

		//_refresher.update(delta);

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
	}*/
}

void LsdjOverlay::onRender(Canvas& canvas) {
	_canvas.clear();
	//_canvas.text(0, 0, "SHIT", lsdj::ColorSets::Normal);

	/*if (_refresher.getOverlay()) {
		Image& target = _canvas.getRenderTarget();
		_refresher.getOverlay()->getBuffer().copyTo(&target.getBuffer());
	}*/

	LsdjCanvasView::onRender(canvas);
}
