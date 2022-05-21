#include "SamplerView.h"

#include <nanovg.h>

#include "core/Project.h"
#include "lsdj/Rom.h"
#include "lsdj/KitUtil.h"
#include "lsdj/OffsetLookup.h"
#include "platform/FileDialog.h"
#include "util/StringUtil.h"
#include "util/DataBuffer.h"
#include "ui/KeyToButton.h"
#include "ui/LsdjModel.h"

using namespace rp;

const std::vector<FileDialogFilter> SAMPLE_FILTER = {
	{ "WAV Files", "*.wav" },
	{ "FLAC Files", "*.flac" },
	{ "MP3 Files", "*.mp3" }
};

const std::vector<FileDialogFilter> KIT_FILTER = {
	{ "LSDj Kit Files", "*.kit" }
};

const Rect<uint32> BOX_SIZE = Rect<uint32>(6, 12, 13, 5);
const Rect<uint32> BOX_AREA = { BOX_SIZE.x * (uint32)lsdj::TILE_WIDTH, BOX_SIZE.y * (uint32)lsdj::TILE_HEIGHT, BOX_SIZE.w * (uint32)lsdj::TILE_WIDTH, BOX_SIZE.h * (uint32)lsdj::TILE_HEIGHT };

SamplerView::SamplerView() : LsdjCanvasView({ 160, 144 }), _ui(_canvas) {
	setType<SamplerView>();
	_waveView = addChildAt<WaveView>("SamplerWaveView", BOX_AREA);
}

void SamplerView::setSystem(SystemWrapperPtr& system) {
	_system = system;

	lsdj::Rom rom = system->getSystem()->getMemory(MemoryType::Rom, AccessType::Read);
	if (rom.isValid()) {
		_canvas.setFont(rom.getFont(1));
		_canvas.setPalette(rom.getPalette(0));

		LsdjModelPtr model = _system->getModel<LsdjModel>();
		int32 selectedKit = 999;

		for (auto& kit : model->_kits) {
			if (kit.first < selectedKit) {
				selectedKit = kit.first;
			}
		}

		if (selectedKit == 999) {
			selectedKit = 0;
		}

		_samplerState.selectedKit = selectedKit;
		_samplerState.selectedSample = 0;
	}

	updateWaveform();
}

void SamplerView::setSampleIndex(KitIndex kitIdx, size_t sampleIdx) {
	_samplerState.selectedKit = (int32)kitIdx;
	_samplerState.selectedSample = (int32)sampleIdx;
	updateWaveform();
}

void SamplerView::onInitialized() {
	// TODO: This should happen in the LsdjModel
	updateSampleBuffers();
}

bool SamplerView::onDrop(const std::vector<std::string>& paths) {
	if (!_system || _samplerState.selectedKit == -1) {
		return false;
	}

	addKitSamples(_samplerState.selectedKit, paths);

	return true;
}

bool SamplerView::onKey(VirtualKey::Enum key, bool down) {
	ButtonType::Enum button = keyToButton(key);

	if (key == VirtualKey::W) {
		_bHeld = down;
	}

	if (_bHeld && down) {
		if (key == VirtualKey::LeftArrow) {
			_samplerState.selectedKit--;

			if (_samplerState.selectedKit < 0) {
				_samplerState.selectedKit = 0;
			}

			updateWaveform();

			return true;
		} else if (key == VirtualKey::RightArrow) {
			_samplerState.selectedKit++;

			if (_samplerState.selectedKit >= lsdj::Rom::KIT_COUNT) {
				_samplerState.selectedKit = lsdj::Rom::KIT_COUNT - 1;
			}

			updateWaveform();

			return false;
		}
	}

	if (key == VirtualKey::Esc) {
		if (down) {
			// Generate menu
			MenuPtr menu = std::make_shared<Menu>();
			buildMenu(*menu);

			MenuViewPtr menuView = addChild<MenuView>("Menu");
			menuView->setMenu(menu);
			menuView->focus();
		}
	} else {
		if (down) {
			if (key == VirtualKey::D) {
				_aHeld = true;
			}

			_ui.pressKey(key);
		} else {
			if (key == VirtualKey::D && _aHeld) {
				_aHeld = false;
				LsdjModelPtr model = _system->getModel<LsdjModel>();
				if (model) {
					model->setRequiresSave(true);
				}
			}

			_ui.releaseKey(key);
		}

		if (button != ButtonType::MAX) {
			if (down) {
				_ui.pressButton(button);
			} else {
				_ui.releaseButton(button);
			}
		}
	}

	return true;
}

void SamplerView::onUpdate(f32 delta) {
	if (!_system) {
		return;
	}
}

bool defaultHexSpin(lsdj::Ui& ui, uint32 x, uint32 y, int32& value, int32 defaultValue, uint32 min, uint32 max, bool editable) {
	lsdj::SpinOptions::Enum spinOptions = lsdj::SpinOptions::None;

	int32 editValue = value;

	if (!editable) {
		spinOptions = (lsdj::SpinOptions::Enum)(spinOptions | lsdj::SpinOptions::Disabled);
	}

	if (value < 0) {
		spinOptions = (lsdj::SpinOptions::Enum)(spinOptions | lsdj::SpinOptions::Dimmed);
		editValue = defaultValue;
	}

	if (ui.buttonDown(ButtonType::B)) {
		if (ui.buttonPressed(ButtonType::A)) {
			value = -1;
			return true;
		}
	}

	if (ui.hexSpin(x, y, editValue, min, max, spinOptions)) {
		value = editValue;
		return true;
	}

	return false;
}

template <const int ItemCount>
bool defaultSelect(lsdj::Ui& ui, uint32 x, uint32 y, int32& value, int32 defaultValue, const std::array<std::string_view, ItemCount>& items, bool editable) {
	lsdj::SelectOptions::Enum spinOptions = lsdj::SelectOptions::None;

	int32 editValue = value;

	if (!editable) {
		spinOptions = (lsdj::SelectOptions::Enum)(spinOptions | lsdj::SelectOptions::Disabled);
	}

	if (value < 0) {
		spinOptions = (lsdj::SelectOptions::Enum)(spinOptions | lsdj::SelectOptions::Dimmed);
		editValue = defaultValue;
	}

	if (ui.buttonDown(ButtonType::B)) {
		if (ui.buttonPressed(ButtonType::A)) {
			value = -1;
			return true;
		}
	}

	if (ui.select<ItemCount>(x, y, editValue, items, spinOptions)) {
		value = editValue;
		return true;
	}

	return false;
}

void SamplerView::onRender() {
	if (!_system) {
		return;
	}

	LsdjModelPtr model = _system->getModel<LsdjModel>();

	lsdj::Rom rom = _system->getSystem()->getMemory(MemoryType::Rom, AccessType::ReadWrite);
	if (!rom.isValid()) {
		return;
	}

	auto found = model->_kits.find(_samplerState.selectedKit);
	bool isEditable = found != model->_kits.end();
	bool editingGlobal = true;

	SampleSettings defaultSettings;
	SampleSettings emptySettings = EMPTY_SAMPLE_SETTINGS;
	SampleSettings* settings = &defaultSettings;
	SampleSettings* globalSettings = &defaultSettings;

	if (isEditable) {
		globalSettings = &found->second.settings;

		if (_samplerState.selectedSample == 0) {
			settings = globalSettings;
		} else if (_samplerState.selectedSample > 0 && _samplerState.selectedSample <= (int32)found->second.samples.size()) {
			settings = &found->second.samples[_samplerState.selectedSample - 1].settings;
			editingGlobal = false;
		} else {
			settings = &emptySettings;
			isEditable = false;
		}
	}

	_canvas.clear();
	_ui.startFrame();

	lsdj::Canvas& _c = _canvas;
	_c.setTranslation(0, 0);
	_ui.handleNavigation();

	Dimension<uint32> dimensionTiles(_c.getDimensions().w / lsdj::TILE_WIDTH, _c.getDimensions().h / lsdj::TILE_HEIGHT);

	uint8 kitIdx = _samplerState.selectedKit != -1 ? (uint8)_samplerState.selectedKit : 0;

	_c.fill(0, 0, dimensionTiles.w, dimensionTiles.h, lsdj::ColorSets::Normal, 0);
	_c.text(3, 0, "KIT    -", lsdj::ColorSets::Normal);
	_c.hexNumber(7, 0, kitIdx + 1, lsdj::ColorSets::Normal);

	if (!rom.kitIsEmpty(kitIdx)) {
		_c.text(12, 0, rom.getKitName(kitIdx), lsdj::ColorSets::Normal);
	} else {
		_c.text(12, 0, "EMPTY", lsdj::ColorSets::Normal);
	}

	_c.text(0, 2, " ", lsdj::ColorSets::Selection);

	std::array<std::string_view, lsdj::Kit::MAX_SAMPLES + 1> sampleNames;
	sampleNames[0] = "ALL";

	for (size_t i = 0; i < lsdj::Kit::MAX_SAMPLES; ++i) {
		_c.hexNumber(0, (uint32)i + 3, (uint8)i, lsdj::ColorSets::Selection, false);
		sampleNames[i + 1] = "---";
	}

	if (!rom.kitIsEmpty(kitIdx)) {
		for (size_t i = 0; i < lsdj::Kit::MAX_SAMPLES; ++i) {
			std::string_view sampleName = rom.getKitSampleName(kitIdx, i);

			if (sampleName == "N/A") {
				if (i < found->second.samples.size()) {
					sampleName = found->second.samples[i].name;
					_c.drawTile(1, i + 3, lsdj::FontTiles::Special, lsdj::ColorSets::Shaded, true);
				} else {
					sampleName = "---";
				}
			}

			sampleNames[i + 1] = sampleName;
		}
	}

	if (_ui.list(2, 2, _samplerState.selectedSample, sampleNames)) {
		updateWaveform();
	}

	_ui.pushColumn();

	const uint32 propertyName = 6;
	const uint32 propertyValue = 14;

	_c.text(propertyName, 2, "NAME", lsdj::ColorSets::Normal);

	if (_samplerState.selectedSample == 0) {
		std::string kitName = std::string(rom.getKitName(kitIdx));

		if (isEditable) {
			if (_ui.textBox(13, 2, kitName, lsdj::Kit::NAME_SIZE)) {
				rom.setKitName(kitIdx, kitName);
				model->setRequiresSave(true);
			}
		} else {
			_c.text(13, 2, kitName, lsdj::ColorSets::Normal);
		}
	} else if (_samplerState.selectedSample > 0) {
		int32 sampleIdx = _samplerState.selectedSample - 1;
		std::string sampleName = std::string(rom.getKitSampleName(kitIdx, sampleIdx));

		if (isEditable) {
			if (_ui.textBox(16, 2, sampleName, lsdj::Kit::SAMPLE_NAME_SIZE)) {
				rom.setKitSampleName(kitIdx, sampleIdx, sampleName);
				model->setRequiresSave(true);
			}
		} else {
			_c.text(16, 2, sampleName, lsdj::ColorSets::Normal);
		}
	}

	lsdj::SelectOptions::Enum selectOptions = isEditable ? lsdj::SelectOptions::None : lsdj::SelectOptions::Disabled;
	lsdj::SpinOptions::Enum spinOptions = isEditable ? lsdj::SpinOptions::None : lsdj::SpinOptions::Disabled;

	_c.text(propertyName, 4, "DITHER", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 4, settings->dither, globalSettings->dither, 0, 0xFF, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 5, "VOL", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 5, settings->volume, globalSettings->volume, 0, 0xFF, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 6, "GAIN", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 6, settings->gain, globalSettings->gain, 0x1, 0xF, isEditable)) {
		updateSampleBuffers();
	}
	/*if (defaultSelect<10>(_ui, 19, 6, settings->gain, globalSettings->gain, {"1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x", "9x", "10x"}, isEditable)) {
		updateSampleBuffers();
	}*/

	_c.text(propertyName, 7, "PITCH", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 7, settings->pitch, globalSettings->pitch, 0, 0xFF, isEditable)) {
		//updateSampleBuffers();
	}



	_c.text(propertyName, 8, "FILTER", lsdj::ColorSets::Normal);
	if (defaultSelect<5>(_ui, 19, 8, settings->filter, globalSettings->filter, { "NONE", "LOWP", "HIGHP", "BANDP", "ALLP" }, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 9, "CUTOFF", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 9, settings->cutoff, globalSettings->cutoff, 0, 0xFF, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 10, "Q", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 10, settings->q, globalSettings->q, 0, 0xF, isEditable)) {
		updateSampleBuffers();
	}

	_c.fill(propertyName, 12, 13, 5, lsdj::ColorSets::Shaded, 0);

	_ui.popColumn();

	_ui.endFrame();

	LsdjCanvasView::onRender();
}

void populateEditKit(SystemPtr system, Menu& target) {
	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);

	for (size_t i = 0; i < lsdj::Rom::KIT_COUNT; ++i) {
		if (rom.getKit(i).isValid()) {
			target.action(fmt::format("{}: {}", i, rom.getKitName(i)), [system]() {
				lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
				//rom.removeKit(i);
			});
		}
	}
}

void populateRemoveKit(SystemPtr system, Menu& target) {
	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);

	for (size_t i = 0; i < lsdj::Rom::KIT_COUNT; ++i) {
		if (rom.getKit(i).isValid()) {
			target.action(fmt::format("{}: {}", i, rom.getKitName(i)), [system]() {
				lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
				//rom.removeKit(i);
			});
		}
	}
}

void exportKitDialog(SystemPtr system, KitIndex kitIdx) {
	std::string target;

	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
	lsdj::Kit kit = rom.getKit(kitIdx);

	if (kit.isValid()) {
		std::string filename = fmt::format("{}.kit", kit.getName());

		if (FileDialog::fileSaveData(nullptr, kit.getBuffer(), KIT_FILTER, filename)) {
			spdlog::info("Saved kit to {}", filename);
		} else {
			spdlog::error("Failed to write kit to {}", filename);
		}
	}
}

void SamplerView::buildMenu(Menu& target) {
	LsdjModelPtr model = _system->getModel<LsdjModel>();
	bool kitEditable = model->_kits.find(_samplerState.selectedKit) != model->_kits.end();

	target.title("LSDJ Sample Manager")
		.separator()
		.action("Add Kit...", [this]() { loadSampleDialog(-1); })
		.action("Add Samples...", [this]() { loadSampleDialog(_samplerState.selectedKit); }, kitEditable)
		.separator()
		.action("Export Kit...", [this]() { exportKitDialog(_system->getSystem(), _samplerState.selectedKit); })
		.separator();

	populateEditKit(_system->getSystem(), target.subMenu("Edit Kit"));
	populateRemoveKit(_system->getSystem(), target.subMenu("Remove Kit"));

	target.separator()
		.action("Close", [this]() { this->remove(); });
}

void SamplerView::loadSampleDialog(KitIndex kitIdx) {
	std::vector<std::string> files;

	if (FileDialog::basicFileOpen(nullptr, files, SAMPLE_FILTER, true)) {
		addKitSamples(kitIdx, files);
	}
}

void SamplerView::addKitSamples(KitIndex kitIdx, const std::vector<std::string>& paths) {
	SystemPtr system = _system->getSystem();
	LsdjModelPtr model = _system->getModel<LsdjModel>();
	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);

	bool newKit = rom.kitIsEmpty(kitIdx);

	std::vector<std::string> kitSamples;

	for (const std::string& path : paths) {
		if (fsutil::getFileExt(path) == ".kit") {
			model->addKit(system, path, kitIdx);
			kitSamples.clear();
			break;
		}

		if (fsutil::getFileExt(path) == ".wav") {
			kitSamples.push_back(path);
		}
	}

	if (kitSamples.size() > 0) {
		std::string kitName = fsutil::getDirectoryName(kitSamples[0]);
		model->addKitSamples(system, paths, kitName, kitIdx);
	}

	_samplerState.selectedKit = (int32)kitIdx;
	_samplerState.selectedSample = 0;

	updateWaveform();

	if (newKit) {
		_system->reset();
	}
}

void SamplerView::updateSampleBuffers() {
	if (!_system || _samplerState.selectedKit < 0) {
		return;
	}

	LsdjModelPtr model = _system->getModel<LsdjModel>();

	auto found = model->_kits.find(_samplerState.selectedKit);
	if (found != model->_kits.end()) {
		model->updateKit(_samplerState.selectedKit);
		updateWaveform();
	}
}

void SamplerView::updateWaveform() {
	lsdj::Rom rom = _system->getSystem()->getMemory(MemoryType::Rom, AccessType::Read);
	if (!rom.isValid()) {
		return;
	}

	size_t kitIdx = _samplerState.selectedKit;
	Uint8Buffer sampleData;
	std::vector<f32> markers;

	if (_samplerState.selectedSample == 0) {
		for (size_t i = 0; i < lsdj::Kit::MAX_SAMPLES; ++i) {
			if (rom.kitSampleExists(kitIdx, i)) {
				markers.push_back((f32)sampleData.size() * 2);
				sampleData.append(rom.getKitSampleData(kitIdx, i));
			} else {
				break;
			}
		}
	} else if (_samplerState.selectedSample > 0) {
		size_t idx = (size_t)_samplerState.selectedSample - 1;
		sampleData = rom.getKitSampleData(kitIdx, idx);
	}

	if (sampleData.size() > 0) {
		Float32Buffer samples;
		lsdj::SampleUtil::convertNibblesToF32(sampleData, samples);
		_waveView->setAudioData(std::move(samples));
		_waveView->setMarkers(std::move(markers));
	} else {
		_waveView->clearWaveform();
	}
}
