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

const Rect<uint32> BOX_SIZE = Rect<uint32>(6, 12, 13, 5);
const Rect<uint32> BOX_AREA = { BOX_SIZE.x * lsdj::TILE_WIDTH, BOX_SIZE.y * lsdj::TILE_HEIGHT, BOX_SIZE.w * lsdj::TILE_WIDTH, BOX_SIZE.h * lsdj::TILE_HEIGHT };

SamplerView::SamplerView() : LsdjCanvasView({ 160, 144 }), _ui(_canvas) {
	setType<SamplerView>();
	_waveView = addChildAt<WaveView>("SamplerWaveView", BOX_AREA);
}

void SamplerView::setSystem(SystemPtr& system) {
	_system = system;

	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
	if (rom.isValid()) {
		_canvas.setFont(rom.getFont(1));
		_canvas.setPalette(rom.getPalette(0));

		_samplerState.selectedKit = 0;// rom.getNextEmptyKit().getIndex();
	}

	updateSampleBuffers();
}

void SamplerView::onInitialized() {
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
			_ui.pressKey(key);
		} else {
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

	LsdjModel* model = getModel();

	lsdj::Rom rom = _system->getMemory(MemoryType::Rom, AccessType::Read);
	if (!rom.isValid()) {
		return;
	}

	auto found = model->kits.find(_samplerState.selectedKit);
	bool isEditable = found != model->kits.end();
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
		if (_ui.textBox(13, 2, kitName, lsdj::Kit::NAME_SIZE)) {
			rom.setKitName(kitIdx, kitName);
		}
	} else if (_samplerState.selectedSample > 0) {
		int32 sampleIdx = _samplerState.selectedSample - 1;
		std::string sampleName = std::string(rom.getKitSampleName(kitIdx, sampleIdx));

		if (_ui.textBox(16, 2, sampleName, lsdj::Kit::SAMPLE_NAME_SIZE)) {
			rom.setKitSampleName(kitIdx, sampleIdx, sampleName);
		}
	}

	lsdj::SelectOptions::Enum selectOptions = isEditable ? lsdj::SelectOptions::None : lsdj::SelectOptions::Disabled;
	lsdj::SpinOptions::Enum spinOptions = isEditable ? lsdj::SpinOptions::None : lsdj::SpinOptions::Disabled;

	_c.text(propertyName, 4, "DITHER", lsdj::ColorSets::Normal);
	defaultSelect<2>(_ui, 19, 4, settings->dither, globalSettings->dither, { "OFF", "ON" }, isEditable);

	_c.text(propertyName, 5, "VOL", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 5, settings->volume, globalSettings->volume, 0, 0xFF, isEditable)) {
		updateSampleBuffers();
	}

	_c.text(propertyName, 6, "PITCH", lsdj::ColorSets::Normal);
	if (defaultHexSpin(_ui, 19, 6, settings->pitch, globalSettings->pitch, 0, 0xFF, isEditable)) {
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

void SamplerView::buildMenu(Menu& target) {
	target.title("LSDJ Sample Manager")
		.separator()
		.action("Add Kit...", [this]() { loadSampleDialog(-1); })
		.subMenu("Edit Kit")
			.parent()
		.subMenu("Remove Kit")
			.parent()
		.separator()
		.action("Close", [this]() { this->remove(); });
}

void SamplerView::loadSampleDialog(KitIndex kitIdx) {
	std::vector<std::string> files;

	if (FileDialog::basicFileOpen(nullptr, files, SAMPLE_FILTER, true)) {
		addKitSamples(kitIdx, files);
	}
}

void SamplerView::addKitSamples(KitIndex kitIdx, const std::vector<std::string>& paths) {
	lsdj::Rom rom = _system->getMemory(MemoryType::Rom, AccessType::Read);
	if (!rom.isValid()) {
		return;
	}

	bool newKit = rom.kitIsEmpty(kitIdx);
	if (kitIdx == -1) {
		kitIdx = rom.nextEmptyKitIdx();
		newKit = true;
	}

	if (kitIdx != -1) {
		std::string kitName = fsutil::getDirectoryName(paths[0]);
		kitName = kitName.substr(0, std::min(lsdj::Kit::NAME_SIZE, kitName.size()));

		KitState kitState = KitState{
			.name = StringUtil::toUpper(kitName),
		};

		for (const std::string& path : paths) {
			std::string sampleName = fsutil::getFilename(path);
			sampleName = fsutil::removeFileExt(sampleName);
			sampleName = sampleName.substr(0, std::min(lsdj::Kit::SAMPLE_NAME_SIZE, sampleName.size()));

			kitState.samples.push_back(KitSample{
				.name = StringUtil::toUpper(sampleName),
				.path = path
			});
		}

		LsdjModel* model = getModel();

		model->kits[kitIdx] = kitState;
		_samplerState.selectedKit = (int32)kitIdx;
		_samplerState.selectedSample = 0;

		updateSampleBuffers();

		if (newKit) {
			_system->reset();
		}
	}
}

LsdjModel* SamplerView::getModel() {
	if (_system) {
		return getShared<Project>()->getModel<LsdjModel>(_system->getId()).get();
	}

	return nullptr;
}

void SamplerView::updateSampleBuffers() {
	if (!_system || _samplerState.selectedKit < 0) {
		return;
	}

	LsdjModel* model = getModel();

	auto found = model->kits.find(_samplerState.selectedKit);
	if (found != model->kits.end()) {
		model->updateKit(_samplerState.selectedKit);
		updateWaveform();
	}
}

void SamplerView::updateWaveform() {
	lsdj::Rom rom = _system->getMemory(MemoryType::Rom, AccessType::Read);
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
