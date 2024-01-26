#include "SamplerView.h"

#include "core/Project.h"
#include "lsdj/Rom.h"
#include "lsdj/KitUtil.h"
#include "lsdj/OffsetLookup.h"
#include "ui/FileDialog.h"
#include "foundation/StringUtil.h"
#include "foundation/DataBuffer.h"
#include "foundation/KeyToButton.h"

using namespace rp;

const std::vector<fw::FileDialogFilter> SAMPLE_FILTER = {
	{ "WAV Files", "*.wav" },
	{ "FLAC Files", "*.flac" },
	{ "MP3 Files", "*.mp3" }
};

const std::vector<fw::FileDialogFilter> KIT_FILTER = {
	{ "LSDj Kit Files", "*.kit" }
};

const fw::RectT BOX_SIZE = fw::RectT(6, 12, 13, 5);
const fw::RectT BOX_AREA = { BOX_SIZE.x * (int32)lsdj::TILE_WIDTH, BOX_SIZE.y * (int32)lsdj::TILE_HEIGHT, BOX_SIZE.w * (int32)lsdj::TILE_WIDTH, BOX_SIZE.h * (int32)lsdj::TILE_HEIGHT };

SamplerView::SamplerView() : LsdjCanvasView({ 160, 144 }), _ui(_canvas) {
	getLayout().setMinDimensions({ 160, 144 });
}

void SamplerView::setSystem(SystemPtr& system, SystemServicePtr& service) {
	_system = system;
	_service = service;

	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
	if (rom.isValid()) {
		_canvas.setFont(rom.getFont(1));
		_canvas.setPalette(rom.getPalette(0));

		LsdjServiceSettings& settings = _service->getStateAs<LsdjServiceSettings>();
		int32 selectedKit = 999;

		for (auto& kit : settings.kits) {
			if (kit.first < selectedKit) {
				selectedKit = (int32)kit.first;
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

void SamplerView::onInitialize() {
	_waveView = addChildAt<fw::WaveView>("SamplerWaveView", BOX_AREA);
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

bool SamplerView::onKey(const fw::KeyEvent& ev) {
	if (ev.key == fw::VirtualKey::Tab) {
		// TODO: This is temporary.  Ideally there will be a global key handler that picks up tabs for moving between instances etc!
		return false;
	}

	fw::ButtonType button = fw::keyToButton(ev.key);

	if (ev.key == fw::VirtualKey::W) {
		_bHeld = ev.down;
	}

	if (_bHeld && ev.down) {
		if (ev.key == fw::VirtualKey::LeftArrow) {
			_samplerState.selectedKit--;

			if (_samplerState.selectedKit < 0) {
				_samplerState.selectedKit = 0;
			}

			updateWaveform();
		} else if (ev.key == fw::VirtualKey::RightArrow) {
			_samplerState.selectedKit++;

			if (_samplerState.selectedKit >= lsdj::Rom::KIT_COUNT) {
				_samplerState.selectedKit = lsdj::Rom::KIT_COUNT - 1;
			}

			updateWaveform();
		}

		return true;
	}

	if (ev.key == fw::VirtualKey::Esc) {
		if (ev.down) {
			// Generate menu
			fw::MenuPtr menu = std::make_shared<fw::Menu>();
			buildMenu(*menu);

			MenuViewPtr menuView = addChild<MenuView>("Menu");
			menuView->setMenu(menu);
			menuView->focus();
		}

		return true;
	}

	if (ev.down) {
		if (ev.key == fw::VirtualKey::D) {
			_aHeld = true;
		}

		_ui.pressKey(ev.key);
	} else {
		if (ev.key == fw::VirtualKey::D && _aHeld) {
			_aHeld = false;
			//LsdjModelPtr model;// = _system->getModel<LsdjModel>();
			//if (model) {
				//settings.setRequiresSave(true);
			//}
		}

		_ui.releaseKey(ev.key);
	}

	if (button != fw::ButtonType::MAX) {
		if (ev.down) {
			_ui.pressButton(button);
		} else {
			_ui.releaseButton(button);
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

	if (ui.buttonDown(fw::ButtonType::B)) {
		if (ui.buttonPressed(fw::ButtonType::A)) {
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

	if (ui.buttonDown(fw::ButtonType::B)) {
		if (ui.buttonPressed(fw::ButtonType::A)) {
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

void SamplerView::onRender(fw::Canvas& canvas) {
	if (!_system) {
		return;
	}

	LsdjServiceSettings& settings = _service->getStateAs<LsdjServiceSettings>();

	lsdj::Rom rom = _system->getMemory(MemoryType::Rom, AccessType::ReadWrite);
	if (!rom.isValid()) {
		return;
	}

	auto found = settings.kits.find(_samplerState.selectedKit);
	bool isEditable = found != settings.kits.end();
	bool editingGlobal = true;

	SampleSettings defaultSettings;
	SampleSettings emptySettings = EMPTY_SAMPLE_SETTINGS;
	SampleSettings* sampleSettings = &defaultSettings;
	SampleSettings* globalSettings = &defaultSettings;

	if (isEditable) {
		globalSettings = &found->second.settings;

		if (_samplerState.selectedSample == 0) {
			sampleSettings = globalSettings;
		} else if (_samplerState.selectedSample > 0 && _samplerState.selectedSample <= (int32)found->second.samples.size()) {
			sampleSettings = &found->second.samples[_samplerState.selectedSample - 1].settings;
			editingGlobal = false;
		} else {
			sampleSettings = &emptySettings;
			isEditable = false;
		}
	}

	_canvas.clear();
	_ui.startFrame();

	lsdj::Canvas& _c = _canvas;
	_c.setTranslation(0, 0);
	_ui.handleNavigation();

	fw::DimensionT<uint32> dimensionTiles(_c.getDimensions().w / lsdj::TILE_WIDTH, _c.getDimensions().h / lsdj::TILE_HEIGHT);

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
		size_t sampleCount = 0;
		for (size_t i = 0; i < lsdj::Kit::MAX_SAMPLES; ++i) {
			std::string_view sampleName = rom.getKitSampleName(kitIdx, i);

			if (sampleName == "N/A") {
				sampleName = "---";
			} else {
				sampleCount++;
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
				//settings.setRequiresSave(true);
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
				//settings.setRequiresSave(true);
			}
		} else {
			_c.text(16, 2, sampleName, lsdj::ColorSets::Normal);
		}
	}

	lsdj::SelectOptions::Enum selectOptions = isEditable ? lsdj::SelectOptions::None : lsdj::SelectOptions::Disabled;
	lsdj::SpinOptions::Enum spinOptions = isEditable ? lsdj::SpinOptions::None : lsdj::SpinOptions::Disabled;

	_c.text(propertyName, 4, "DITHER", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 4, sampleSettings->dither, globalSettings->dither, 0, 0xFF, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 5, "VOL", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 5, sampleSettings->volume, globalSettings->volume, 0, 0xFF, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 6, "GAIN", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 6, sampleSettings->gain, globalSettings->gain, 0x1, 0xF, isEditable)) {
		updateSampleBuffers();
	}
	/*if (defaultSelect<10>(_ui, 19, 6, settings->gain, globalSettings->gain, {"1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x", "9x", "10x"}, isEditable)) {
		updateSampleBuffers();
	}*/

	_c.text(propertyName, 7, "PITCH", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 7, sampleSettings->pitch, globalSettings->pitch, 0, 0xFF, isEditable)) {
		//updateSampleBuffers();
	}



	_c.text(propertyName, 8, "FILTER", lsdj::ColorSets::Normal);
	if (defaultSelect<5>(_ui, 19, 8, sampleSettings->filter, globalSettings->filter, { "NONE", "LOWP", "HIGHP", "BANDP", "ALLP" }, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 9, "CUTOFF", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 9, sampleSettings->cutoff, globalSettings->cutoff, 0, 0xFF, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 10, "Q", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 10, sampleSettings->q, globalSettings->q, 0, 0xF, isEditable)) {
		updateSampleBuffers();
	}

	_c.fill(propertyName, 12, 13, 5, lsdj::ColorSets::Shaded, 0);

	_ui.popColumn();

	_ui.endFrame();

	LsdjCanvasView::onRender(canvas);
}

void populateEditKit(SystemPtr system, fw::Menu& target) {
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

void populateRemoveKit(SystemPtr system, fw::Menu& target) {
	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);

	for (size_t i = 0; i < lsdj::Rom::KIT_COUNT; ++i) {
		if (rom.getKit(i).isValid()) {
			target.action(fmt::format("{}: {}", i, rom.getKitName(i)), [system]() {
				lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Write);
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

		if (fw::FileDialog::fileSaveData(nullptr, kit.getBuffer(), KIT_FILTER, filename)) {
			spdlog::info("Saved kit to {}", filename);
		} else {
			spdlog::error("Failed to write kit to {}", filename);
		}
	}
}

void SamplerView::buildMenu(fw::Menu& target) {
	const LsdjServiceSettings& settings = _service->getStateAs<LsdjServiceSettings>();
	bool kitEditable = settings.kits.find(_samplerState.selectedKit) != settings.kits.end();

	target.title("LSDJ Sample Manager")
		.separator()
		.action("Add Kit...", [this]() { loadSampleDialog(-1); })
		.action("Add Samples...", [this]() { loadSampleDialog(_samplerState.selectedKit); }, kitEditable)
		.separator()
		.action("Export Kit...", [this]() { exportKitDialog(_system, _samplerState.selectedKit); })
		.separator();

	populateEditKit(_system, target.subMenu("Edit Kit"));
	populateRemoveKit(_system, target.subMenu("Remove Kit"));

	target.separator()
		.action("Close", [this]() { this->remove(); });
}

void SamplerView::loadSampleDialog(KitIndex kitIdx) {
	std::vector<std::string> files;

	if (fw::FileDialog::basicFileOpen(nullptr, files, SAMPLE_FILTER, true)) {
		addKitSamples(kitIdx, files);
	}
}

void SamplerView::addKitSamples(KitIndex kitIdx, const std::vector<std::string>& paths) {
	SystemPtr system = _system;
	LsdjServiceSettings& settings = _service->getStateAs<LsdjServiceSettings>();
	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);

	bool newKit = rom.kitIsEmpty(kitIdx);

	std::vector<std::string> kitSamples;

	for (const std::string& path : paths) {
		if (fw::FsUtil::getFileExt(path) == ".kit") {
			KitUtil::addKit(system, settings, path, kitIdx);
			kitSamples.clear();
			break;
		}

		if (fw::FsUtil::getFileExt(path) == ".wav") {
			kitSamples.push_back(path);
		}
	}

	if (kitSamples.size() > 0) {
		std::string kitName = fw::FsUtil::getDirectoryName(kitSamples[0]);
		KitUtil::addKitSamples(system, settings, paths, kitName, kitIdx);
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

	LsdjServiceSettings& settings = _service->getStateAs<LsdjServiceSettings>();

	auto found = settings.kits.find(_samplerState.selectedKit);
	if (found != settings.kits.end()) {
		//settings.updateKit(_samplerState.selectedKit);
		updateWaveform();
	}
}

void SamplerView::updateWaveform() {
	lsdj::Rom rom = _system->getMemory(MemoryType::Rom, AccessType::Read);
	if (!rom.isValid()) {
		return;
	}

	size_t kitIdx = _samplerState.selectedKit;
	fw::Uint8Buffer sampleData;
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
		fw::Float32Buffer samples;
		lsdj::SampleUtil::convertNibblesToF32(sampleData, samples);
		_waveView->setAudioData(std::move(samples), 1);
		//_waveView->setMarkers(std::move(markers));
	} else {
		_waveView->clearWaveform();
	}
}
