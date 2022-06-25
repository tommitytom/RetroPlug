#include "SynthView.h"

#include <nanovg.h>

#include "lsdj/OffsetLookup.h"
#include "ui/KeyToButton.h"
#include "util/HashUtil.h"

using namespace rp;

const Rect WAVE_VIEW_SIZE = Rect(3, 12, 16, 5);
const Rect WAVE_VIEW_AREA = { 
	WAVE_VIEW_SIZE.x * (int32)lsdj::TILE_WIDTH, 
	WAVE_VIEW_SIZE.y * (int32)lsdj::TILE_HEIGHT, 
	WAVE_VIEW_SIZE.w * (int32)lsdj::TILE_WIDTH, 
	WAVE_VIEW_SIZE.h * (int32)lsdj::TILE_HEIGHT 
};

SynthView::SynthView() : LsdjCanvasView({ 160, 144 }), _ui(_canvas) { 
	setType<SynthView>(); 
	_waveView = addChildAt<WaveView>("SynthWaveView", WAVE_VIEW_AREA);
}

void SynthView::setSystem(SystemPtr& system) {
	_system = system;

	lsdj::Rom rom = system->getMemory(MemoryType::Rom, AccessType::Read);
	if (rom.isValid()) {
		lsdj::Palette palette = rom.getPalette(0);

		_canvas.setFont(rom.getFont(0));
		_canvas.setPalette(rom.getPalette(0));

		_waveView->setTheme(WaveView::Theme{
			.foreground = palette.getColor(lsdj::ColorSets::Normal, 2),
			.background = palette.getColor(lsdj::ColorSets::Shaded, 0),
			.selection = palette.getColor(lsdj::ColorSets::Selection, 0),
		});
	}
}

bool SynthView::onDrop(const std::vector<std::string>& paths) {
	if (!_system) {
		return false;
	}

	MemoryAccessor savData = _system->getMemory(MemoryType::Sram, AccessType::ReadWrite);
	if (savData.isValid()) {
		lsdj::Song song((uint8*)savData.getData());

		//size_t sampleIdx;
		//uint16 dataOffset;

		for (size_t i = 0; i < paths.size(); ++i) {
			std::string ext = fs::path(paths[i]).extension().string();

			if (ext == ".snt") {
				Uint8Buffer snt;
				if (fsutil::readFile(paths[i], &snt)) {
					song.setSynthData(_samplerState.selectedSynth, snt);
					updateWaveform(song);
				}
			} else if (ext == ".wav") {

			} else if (ext == ".lua") {

			}
		}

		savData.write(0, song.getBuffer());

		return true;
	}

	return false;
}

bool SynthView::onKey(VirtualKey::Enum key, bool down) {
	ButtonType::Enum button = keyToButton(key);

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

		return true;
	}

	return false;
}

void SynthView::onUpdate(f32 delta) {
	if (!_system) {
		return;
	}
}

PointT<uint32> tileToPixel(PointT<uint32> tile) {
	return { tile.x * (uint32)lsdj::TILE_WIDTH, tile.y * (uint32)lsdj::TILE_HEIGHT };
}

void SynthView::updateWaveform(lsdj::Song& song) {
	if (_samplerState.selectedSynth >= 0) {
		Uint8Buffer synthData = song.getSynthData(_samplerState.selectedSynth);

		Float32Buffer samples;
		lsdj::SampleUtil::convertNibblesToF32(synthData, samples);
		setWaveform(samples);
	}
}

void SynthView::onRender(Canvas& canvas) {
	if (!_system) {
		return;
	}

	MemoryAccessor savData = _system->getMemory(MemoryType::Sram, AccessType::ReadWrite);
	if (!savData.isValid()) {
		return;
	}

	lsdj::Song song((uint8*)savData.getData());

	uint64 sramHash = HashUtil::hash(song.getBuffer());
	if (sramHash != _lastSramHash) {
		updateWaveform(song);
		_lastSramHash = sramHash;
	}

	_canvas.clear();
	_ui.startFrame();

	lsdj::Canvas& _c = _canvas;
	_c.setTranslation(0, 0);
	_ui.handleNavigation();

	DimensionT<uint32> dimensionTiles(_c.getDimensions().w / lsdj::TILE_WIDTH, _c.getDimensions().h / lsdj::TILE_HEIGHT);

	uint8 synthIdx = _samplerState.selectedSynth;

	_c.fill(0, 0, dimensionTiles.w, dimensionTiles.h, lsdj::ColorSets::Normal, 0);
	_c.text(3, 0, "SYNTH", lsdj::ColorSets::Normal);
	_c.hexNumber(10, 0, synthIdx, lsdj::ColorSets::Normal, false);

	_c.text(0, 2, " ", lsdj::ColorSets::Selection);

	std::array<std::string_view, 16> sampleNames = {
		"00","01","02","03","04","05","06","07","08","09","0A","0B","0C","0D","0E","0F"
	};

	if (_ui.list(0, 2, _samplerState.selectedSynth, sampleNames)) {
		updateWaveform(song);
	}

	_ui.pushColumn();

	const uint32 propertyName = 3;
	const uint32 propertyValue = 14;

	_c.text(propertyName, 2, "DITHER", lsdj::ColorSets::Normal);
	_ui.select<2>(19, 2, _samplerState.settings.dither, { "OFF", "ON" });

	_c.text(propertyName, 4, "VOL", lsdj::ColorSets::Normal);
	if (_ui.hexSpin(19, 4, _samplerState.settings.volume)) {
		//repatchSample();
	}

	_c.text(propertyName, 5, "PITCH", lsdj::ColorSets::Normal);
	_ui.hexSpin(19, 5, _samplerState.settings.pitch);

	_c.text(propertyName, 7, "FILTER", lsdj::ColorSets::Normal);
	_ui.select<5>(19, 7, _samplerState.settings.filter, { "NONE", "LOWP", "HIGHP", "BANDP", "ALLP" });

	_c.text(propertyName, 8, "CUTOFF", lsdj::ColorSets::Normal);
	if (_ui.hexSpin(19, 8, _samplerState.settings.cutoff)) {
		//repatchSample();
	}

	_c.text(propertyName, 9, "Q", lsdj::ColorSets::Normal);
	if (_ui.hexSpin(19, 9, _samplerState.settings.q)) {
		//repatchSample();
	}

	_ui.popColumn();
	_ui.endFrame();

	LsdjCanvasView::onRender(canvas);
}

void SynthView::setWaveform(Float32Buffer& samples) {
	/*WaveformBuffer waveform(_waveView->getExpectedSampleCount());
	WaveformUtil::generate(samples, waveform);
	_waveView->setWaveform(std::move(waveform));*/
}
