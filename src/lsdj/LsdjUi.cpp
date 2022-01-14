#include "LsdjUi.h"

#include <cctype>
#include <stb/stb_image.h>

#include "data/font.h"

#include <stb/stb_image_write.h>

using namespace rp;
using namespace rp::lsdj;

void Ui::render(const Song& song, const Ram& state) {
	switch (state.getScreen()) {
	case ScreenType::Song:
		renderSong(song, state);
		break;
	case ScreenType::Chain:
		renderChain(song, state, 0);
		break;
	case ScreenType::Phrase:
		renderPhrase(song, state, 0);
		break;
	}
}

const char* getChannelName(uint8 channel) {
	const char* channelName = "";
	switch (channel) {
	case 0: channelName = "PU1"; break;
	case 1: channelName = "PU2"; break;
	case 2: channelName = "WAV"; break;
	case 3: channelName = "NOI"; break;
	}

	return channelName;
}

void Ui::renderBase(const Ram& state, uint8 channel, ScreenType screenType) {
	if (screenType == ScreenType::Unknown) {
		screenType = state.getScreen();
	}

	_c.fill(0, 0, COL_COUNT, ROW_COUNT, ColorSets::Normal, 0);

	_c.fill(COL_COUNT - 3, 0, 3, 10, ColorSets::Shaded, 0);
	_c.fill(COL_COUNT - 5, ROW_COUNT - 3, 5, 3, ColorSets::Shaded, 0);

	_c.text(COL_COUNT - 3, 0, getChannelName(channel), ColorSets::Shaded);

	_c.drawTile(COL_COUNT - 4, 4, FontTiles::Note, ColorSets::Normal);
	_c.number(COL_COUNT - 3, 4, state.getTempo(), ColorSets::Shaded);

	_c.text(COL_COUNT - 4, 6, "1", ColorSets::Normal);
	_c.text(COL_COUNT - 4, 7, "2", ColorSets::Normal);
	_c.text(COL_COUNT - 4, 8, "W", ColorSets::Normal);
	_c.text(COL_COUNT - 4, 9, "N", ColorSets::Normal);

	_c.text(COL_COUNT - 5, ROW_COUNT - 3, "P", ColorSets::Alternate);
	_c.text(COL_COUNT - 5, ROW_COUNT - 2, "S", ColorSets::Shaded);
	_c.text(COL_COUNT - 4, ROW_COUNT - 2, "CPIT", ColorSets::Alternate);
	_c.text(COL_COUNT - 5, ROW_COUNT - 1, "G", ColorSets::Alternate);
}

void Ui::renderChainData(const Chain& chain, const Ram& state, uint8 channel) {
	for (uint32 i = 0; i < 16; ++i) {
		uint8 step = (uint8)i;
		uint32 yOff = i;// +2;

		if (state.isChannelActive(channel) && state.getChainPosition(channel) == i) {
			_c.drawTile(0, yOff, FontTiles::ArrowRight, ColorSets::Normal);
		}

		uint8 phraseIndex = 0xFF;
		uint8 transpose = 0;

		if (chain.isValid()) {
			phraseIndex = chain.getPhraseIndex(step);
			transpose = chain.getPhraseTransposition(step);
		}

		if (phraseIndex != 0xFF) {
			_c.hexNumber(1, yOff, phraseIndex, ColorSets::Normal);
		} else {
			_c.text(1, yOff, "--", ColorSets::Normal);
		}

		_c.hexNumber(4, yOff, transpose, ColorSets::Normal);
	}
}

void Ui::renderChain(const Song& song, const Ram& state, uint8 channel) {
	renderBase(state, channel, ScreenType::Chain);

	Chain chain = song.getChain(channel, state.getSongPosition(channel));

	_c.text(0, 0, "CHAIN", ColorSets::Normal);
	_c.hexNumber(4, 0, chain.getIndex(), ColorSets::Normal);

	for (uint32 i = 0; i < 16; ++i) {
		_c.hexNumber(0, i, (uint8)i, ColorSets::Alternate, false);
	}

	renderChainData(chain, state, channel);
}

FontTiles findTile(uint32 code) {
	if (code == 45) {
		return FontTiles::Dash;
	}

	if (code == 47) {
		return FontTiles::Slash;
	}

	if (code >= 48 && code <= 57) {
		return (FontTiles)((uint32)FontTiles::Num0 + (code - 48));
	}

	if (code >= 65 && code <= 90) {
		return (FontTiles)((uint32)FontTiles::A + (code - 65));
	}

	return FontTiles::Space;
}

void formatNote(uint8 note, FontTiles* target) {
	if (note == 0) {
		target[0] = FontTiles::Dash;
		target[1] = FontTiles::Dash;
		target[2] = FontTiles::Dash;
		return;
	}

	note -= 1;

	uint8 octave = (note / 12);
	uint8 notePos = note - (octave * 12);

	switch (notePos) {
	case 0: target[0] = FontTiles::C; break;
	case 1: target[0] = FontTiles::C; target[1] = FontTiles::Hash; break;
	case 2: target[0] = FontTiles::D; break;
	case 3: target[0] = FontTiles::D; target[1] = FontTiles::Hash; break;
	case 4: target[0] = FontTiles::E; break;
	case 5: target[0] = FontTiles::F; break;
	case 6: target[0] = FontTiles::F; target[1] = FontTiles::Hash; break;
	case 7: target[0] = FontTiles::G; break;
	case 8: target[0] = FontTiles::G; target[1] = FontTiles::Hash; break;
	case 9: target[0] = FontTiles::A; break;
	case 10: target[0] = FontTiles::A; target[1] = FontTiles::Hash; break;
	case 11: target[0] = FontTiles::B; break;
	}

	target[2] = findNumberTile(octave + 3);
}

void Ui::renderPhraseData(const Phrase& phrase, uint8 playbackOffset) {
	if (phrase.isValid()) {
		for (uint32 i = 0; i < 16; ++i) {
			uint8 step = (uint8)i;
			uint32 yOff = i;

			Instrument instrument = phrase.getInstrument(step);
			lsdj_command_t cmd = phrase.getCommand(step);
			uint8 cmdValue = phrase.getCommandValue(step);
			uint8 note = phrase.getNote(step);

			if (i == playbackOffset) {
				_c.drawTile(0, yOff, FontTiles::ArrowRight, ColorSets::Normal);
			}

			uint32 instrumentOffset = 0;
			uint32 commandOffset = 3;
			bool drawn = false;
			Rom rom;

			if (instrument.isValid()) {
				if (instrument.getType() == LSDJ_INSTRUMENT_TYPE_KIT) {
					uint8 sample1 = note >> 4;
					uint8 sample2 = note & 0x0F;

					if (sample1 == 0) {
						_c.text(1, yOff, "---", ColorSets::Shaded);
					} else {
						std::string_view kit1 = rom.getKitSampleName(instrument.getKit1(), sample1 - 1);
						_c.text(1, yOff, kit1, ColorSets::Shaded);
					}

					if (sample2 == 0) {
						_c.text(4, yOff, "---", ColorSets::Normal);
					} else {
						std::string_view kit2 = rom.getKitSampleName(instrument.getKit2(), sample2 - 1);
						_c.text(4, yOff, kit2, ColorSets::Normal);
					}

					instrumentOffset = 3;
					commandOffset = 5;
					drawn = true;
				}
			}

			if (!drawn) {
				FontTiles noteText[3] = { FontTiles::Space, FontTiles::Space, FontTiles::Space };
				formatNote(note, noteText);

				for (uint8 i = 0; i < 3; ++i) {
					_c.drawTile(1 + i, yOff, noteText[i], ColorSets::Normal);
				}
			}

			_c.text(5 + instrumentOffset, yOff, "I", ColorSets::Shaded);

			if (!instrument.isValid()) {
				_c.text(6 + instrumentOffset, yOff, "--", ColorSets::Normal);
			} else {
				_c.hexNumber(6 + instrumentOffset, yOff, instrument.getIndex(), ColorSets::Normal);
			}

			_c.drawTile(6 + commandOffset, yOff, getCommandTile(cmd), ColorSets::Shaded);

			switch (cmd) {
			case LSDJ_COMMAND_O:
				switch (cmdValue) {
				case 0:
					_c.drawTile(7 + commandOffset, yOff, FontTiles::L, ColorSets::Normal);
					_c.drawTile(8 + commandOffset, yOff, FontTiles::R, ColorSets::Normal);
					break;
				case 1:
					_c.drawTile(7 + commandOffset, yOff, FontTiles::L, ColorSets::Normal);
					//_c.drawDimmedTile(8 + commandOffset, yOff, FontTiles::R, ColorSets::Normal);
					break;
				case 2:
					//_c.drawDimmedTile(7 + commandOffset, yOff, FontTiles::L, ColorSets::Normal);
					_c.drawTile(8 + commandOffset, yOff, FontTiles::R, ColorSets::Normal);
					break;
				}

				break;
			default:
				_c.hexNumber(7 + commandOffset, yOff, cmdValue, ColorSets::Normal);
			}
		}
	} else {
		for (uint32 i = 0; i < 16; ++i) {
			uint8 step = (uint8)i;
			_c.text(1, i, "---", ColorSets::Normal);
			_c.text(5, i, "I", ColorSets::Shaded);
			_c.text(6, i, "--", ColorSets::Normal);
			_c.text(9, i, "-", ColorSets::Shaded);
			_c.text(10, i, "00", ColorSets::Normal);
		}
	}
}

void Ui::renderPhrase(const Song& song, const Ram& state, uint8 channel) {
	renderBase(state, channel, ScreenType::Phrase);

	Chain chain = song.getChain(channel, state.getSongPosition(channel));
	if (!chain.isValid()) {
		return;
	}

	Phrase phrase = chain.getPhrase(state.getChainPosition(channel));
	if (!phrase.isValid()) {
		return;
	}

	_c.text(0, 0, "PHRASE", ColorSets::Normal);
	_c.hexNumber(7, 0, phrase.getIndex(), ColorSets::Normal);

	for (uint32 i = 0; i < 16; ++i) {
		_c.hexNumber(0, i + 2, (uint8)i, ColorSets::Alternate, false);
	}

	_c.translate(1, 2);
	renderPhraseData(phrase, state.isChannelActive(channel) ? state.getPhrasePosition(channel) : 0xFF);
	_c.untranslate();
}

void Ui::renderSongData(const Song& song, const Ram& state, uint32 rowOffset) {
	uint32 songRowCount = _c.getDimensions().h / TILE_HEIGHT - 2;

	for (uint32 y = 0; y < songRowCount; y++) {
		for (uint32 x = 0; x < 4; x++) {
			ColorSets colorSet = song.isRowBookMarked((uint8)x, (uint8)y) ? ColorSets::Shaded : ColorSets::Normal;

			if (x == state.getCursorX() && y == state.getCursorY()) {
				colorSet = ColorSets::Selection;
			}

			uint32 xOff = x * 3;
			uint8 idx = song.getChainIndex(x, y + rowOffset);

			if (state.isChannelActive(x) && state.getSongPosition(x) == y + rowOffset) {
				_c.drawTile(xOff, y, FontTiles::ArrowRight, ColorSets::Normal);
			}

			if (idx != 0xFF) {
				_c.text(xOff + 1, y, fmt::format("{:02x}", idx), colorSet);
			} else {
				_c.text(xOff + 1, y, "--", colorSet);
			}
		}
	}
}

void Ui::renderSong(const Song& song, const Ram& state, uint32 rowOffset) {
	renderBase(state, 0, ScreenType::Song);

	_c.text(0, 0, "SONG", ColorSets::Normal);

	uint32 songRowCount = (uint32)ROW_COUNT - 2;
	for (uint32 y = 0; y < songRowCount; y++) {
		_c.text(0, y + 2, fmt::format("{:02x}", y + rowOffset), ColorSets::Alternate);
	}

	_c.translate(2, 2);
	renderSongData(song, state, rowOffset);
	_c.untranslate();
}

void Ui::renderMode1(const Song& song, const Ram& state) {
	for (uint32 i = 0; i < 4; ++i) {
		_c.setTranslation(i, 0);
		renderChain(song, state, (uint8)i);

		_c.setTranslation(i, 1);
		renderPhrase(song, state, (uint8)i);
	}
}

void Ui::renderMode2(const Song& song, const Ram& state) {
	_c.setTranslation(0, 0);

	uint8 num = 255;
	Dimension<uint32> dimensionTiles(_c.getDimensions().w / TILE_WIDTH, _c.getDimensions().h / TILE_HEIGHT);

	_c.fill(0, 0, dimensionTiles.w, dimensionTiles.h, ColorSets::Normal, 0);

	_c.text(0, 0, "SONG", ColorSets::Normal);

	uint32 songRowCount = dimensionTiles.h - 2;
	uint32 rowOffset = 0;

	for (uint32 y = 0; y < songRowCount; y++) {
		_c.text(0, y + 2, fmt::format("{:02x}", y + rowOffset), ColorSets::Alternate);
	}

	uint32 chainOffsetX = 17;
	uint32 phraseOffsetX = chainOffsetX + 15;
	uint32 phraseWidth = 17;

	_c.setTranslation(2, 0);
	_c.fill(chainOffsetX - 4, 0, 1, dimensionTiles.h, ColorSets::Normal, 1);
	_c.fill(phraseOffsetX - 4, 0, 1, dimensionTiles.h, ColorSets::Normal, 1);

	_c.setTranslation(2, 2);
	renderSongData(song, state, rowOffset);

	// Render chains
	for (uint32 i = 0; i < 4; ++i) {
		_c.setTranslation(chainOffsetX, i * 18);

		uint8 channel = (uint8)i;
		Chain chain = song.getChain(channel, state.getSongPosition(channel));

		_c.text(0, 0, "CHAIN", ColorSets::Normal);
		_c.hexNumber(6, 0, chain.getIndex(), ColorSets::Normal);
		_c.text(9, 0, getChannelName(i), ColorSets::Shaded);

		for (uint8 j = 0; j < 16; ++j) {
			_c.setTranslation(chainOffsetX, i * 18 + 2);
			_c.hexNumber(0, j, (uint8)j, ColorSets::Alternate, false);

			_c.setTranslation(chainOffsetX + 1, i * 18 + 2);
			renderChainData(chain, state, channel);
		}
	}

	for (uint32 i = 0; i < 4; ++i) {
		_c.setTranslation(phraseOffsetX + phraseWidth * i, 0);

		uint8 phraseIndex = LSDJ_CHAIN_NO_PHRASE;
		uint8 channel = (uint8)i;
		Chain chain = song.getChain(channel, state.getSongPosition(channel));
		uint8 chainPosition = state.getChainPosition(channel);
		uint8 groupOffset = chainPosition != 0xFF ? chainPosition / 4 : 0;

		_c.text(0, 0, "PHRASE", ColorSets::Normal);

		if (chain.isValid()) {
			phraseIndex = chain.getPhraseIndex(state.getChainPosition(channel));
			_c.hexNumber(7, 0, phraseIndex, ColorSets::Normal);
		}

		_c.text(11, 0, getChannelName(i), ColorSets::Shaded);

		for (uint8 j = 0; j < 64; ++j) {
			_c.hexNumber(0, j + 2, (uint8)j + groupOffset * 64, ColorSets::Alternate, true);
		}

		for (uint8 j = 0; j < 4; ++j) {
			_c.setTranslation(phraseOffsetX + phraseWidth * i + 2, j * 16 + 2);

			uint8 group = j + groupOffset * 4;

			if (chain.isValid()) {
				Phrase phrase = chain.getPhrase(group);

				if (phrase.isValid()) {
					uint8 phraseOffset = 0xFF;
					if (state.getChainPosition(channel) == group) {
						phraseOffset = phrase.getIndex() == phraseIndex ? state.getPhrasePosition(channel) : 0xFF;
					}

					renderPhraseData(phrase, phraseOffset);
				} else {
					renderPhraseData(Phrase(), 0xFF);
				}
			} else {
				renderPhraseData(Phrase(), 0xFF);
			}
		}
	}
}

void renderList() {

}

void pushPropertyGrid(lsdj::Ui& ui, uint32 x, uint32 y, uint32 width) {

}

void popPropertGrid() {

}

void drawProperty(lsdj::Ui& ui, std::string_view name) {

}

void decodeSamples(Uint8Buffer input, std::vector<f32>& output) {
	output.resize(input.size() * 2);

	for (size_t i = 0; i < input.size(); ++i) {
		uint8 n = input[i];
		output[i * 2] = (f32)(n & 0xF0);
		output[i * 2 + 1] = (f32)((n & 0xF) << 4);
	}

	for (size_t i = 0; i < output.size(); ++i) {
		output[i] = (output[i] - 0x80) / 128.0f;
	}
}
/*
void SamplerUi::addSample(std::string_view name, const std::vector<f32>& samples) {
	Float32Buffer b((f32*)samples.data(), samples.size(), false);
	Uint8Buffer output;
	SampleUtil::convertF32ToNibbles(b, output);

	Rom& rom = (Rom&)_c.getRom();
	Kit kit = rom.getKit(0);
	_sampleIdx = kit.addSample(name, output);

	_samples = samples;

	setWaveform(_samples);
}

const f32 VOLUME_MIN = 0.0f;
const f32 VOLUME_MAX = 2.5f;
const f32 VOLUME_RANGE = VOLUME_MAX - VOLUME_MIN;

struct BiquadCoeffs {
	f32 b0;
	f32 b1;
	f32 b2;
	f32 a0;
	f32 a1;
	f32 a2;
};

const f32 PI = 3.14159265359f;

void lowPassCoeffs(f32 cutoff, f32 q, f32 sampleRate, BiquadCoeffs& target) {
	f32 omega = 2.0f * PI * cutoff / sampleRate;
	f32 s = sin(omega);
	f32 c = cos(omega);
	f32 alpha = s / (2 * q);

	target.b0 = (1.0f - c) / 2.0f;
	target.b1 = 1.0f - c;
	target.b2 = (1.0f - c) / 2.0f;
	target.a0 = 1.0f + alpha;
	target.a1 = -2.0f * c;
	target.a2 = 1.0f - alpha;
}

const f32 SAMPLE_RATE = 11468.0f;
const f32 NYQUIST = SAMPLE_RATE / 2.0f;

const f32 CUTOFF_MAX = NYQUIST;

const f32 Q_MIN = 0.001f;
const f32 Q_MAX = 1.0f;
const f32 Q_RANGE = Q_MAX - Q_MIN;

#include <miniaudio/miniaudio.h>

void SamplerUi::repatchSample() {
	std::vector<f32> samples = _samples;
	std::vector<f32> target;
	target.resize(samples.size());

	// Processing
	
	// Filter

	f32 cutoff = _samplerState.cutoff / 255.0f;
	f32 q = _samplerState.q / 255.0f;

	cutoff *= CUTOFF_MAX;
	q = q * Q_RANGE + Q_MIN;

	BiquadCoeffs coeff;
	lowPassCoeffs(cutoff, q, SAMPLE_RATE, coeff);

	ma_biquad filter;
	ma_biquad_config config = ma_biquad_config_init(ma_format_f32, 1, coeff.b0, coeff.b1, coeff.b2, coeff.a0, coeff.a1, coeff.a2);
	ma_result result = ma_biquad_init(&config, &filter);
	if (result == MA_SUCCESS) {
		ma_biquad_process_pcm_frames(&filter, target.data(), samples.data(), samples.size());
	} else {
		spdlog::warn("Failed to create filter");
		target = samples;
	}

	
	// Gain
	
	f32 vol = _samplerState.volume / 255.0f;
	vol *= VOLUME_RANGE;

	for (size_t i = 0; i < target.size(); ++i) {
		target[i] = target[i] * vol;
	}



	// Clip
	for (size_t i = 0; i < target.size(); ++i) {
		target[i] = std::min(1.0f, std::max(-1.0f, target[i]));
	}

	Float32Buffer b((f32*)target.data(), target.size(), false);
	Uint8Buffer output;
	SampleUtil::convertF32ToNibbles(b, output);

	Rom& rom = (Rom&)_c.getRom();
	Kit kit = rom.getKit(0);
	kit.setSampleData(_sampleIdx, output);

	setWaveform(target);
}

void SamplerUi::setWaveform(std::vector<f32>& samples) {
	size_t targetSampleCount = 8 * 12 * 2;
	size_t chunkSize = samples.size() / targetSampleCount;

	_waveform.clear();
	_waveform.reserve(sampleCount * 2);

	f32 lineMax = boxArea.h / 2;

	for (size_t i = 0; i < targetSampleCount; ++i) {
		f32 min = 999999999.0f;
		f32 max = -999999999.0f;

		for (size_t j = 0; j < chunkSize; ++j) {
			f32 sample = samples[i * chunkSize + j];
			min = std::min(sample, min);
			max = std::max(sample, max);
		}

		f32 top = boxMidY - min * lineMax;
		f32 bottom = boxMidY - max * lineMax;

		if (fabs(top - bottom) < 0.0001) {
			bottom += 2;
		}

		_waveform.push_back(top);
		_waveform.push_back(bottom);
	}
}

void SamplerUi::renderSampler() {
	_c.setTranslation(0, 0);
	_ui.handleNavigation();

	const Rom& rom = _c.getRom();
	Dimension<uint32> dimensionTiles(_c.getDimensions().w / TILE_WIDTH, _c.getDimensions().h / TILE_HEIGHT);

	uint8 kitIdx = 0;

	_c.fill(0, 0, dimensionTiles.w, dimensionTiles.h, ColorSets::Normal, 0);
	_c.text(3, 0, "KIT    -", ColorSets::Normal);
	_c.hexNumber(7, 0, kitIdx, ColorSets::Normal);
	_c.text(12, 0, rom.getKitName(kitIdx), ColorSets::Normal);

	_c.text(0, 2, " ", ColorSets::Selection);

	std::array<std::string_view, lsdj::Kit::MAX_SAMPLES + 1> sampleNames;
	sampleNames[0] = "ALL";

	for (size_t i = 0; i < lsdj::Kit::MAX_SAMPLES; ++i) {
		_c.hexNumber(0, i + 3, (uint8)i, ColorSets::Selection, false);

		std::string_view sampleName = rom.getKitSampleName(kitIdx, i);

		if (sampleName == "N/A") {
			sampleName = "---";
		}

		sampleNames[i + 1] = sampleName;
	}

	if (_ui.list(2, 2, _samplerState.selectedSample, sampleNames)) {
		// Update waveform
		if (_samplerState.selectedSample > 0) {
			size_t idx = (size_t)_samplerState.selectedSample - 1;
			Uint8Buffer buffer = rom.getKitSampleData(kitIdx, idx);

			if (buffer.size() > 0) {
				std::vector<f32> samples;
				decodeSamples(buffer, samples);
				setWaveform(samples);
			}
		}
	}

	_ui.pushColumn();

	const uint32 propertyName = 6;
	const uint32 propertyValue = 14;

	_c.text(propertyName, 2, "NAME", ColorSets::Normal);

	if (_samplerState.selectedSample == 0) {
		_c.text(13, 2, rom.getKitName(kitIdx), ColorSets::Shaded);
	} else if (_samplerState.selectedSample > 0) {
		_c.text(16, 2, rom.getKitSampleName(kitIdx, _samplerState.selectedSample - 1), ColorSets::Shaded);
	}

	_c.text(propertyName, 4, "DITHER", ColorSets::Normal);
	_ui.select<2>(19, 4, _samplerState.dither, { "OFF", "ON" });

	_c.text(propertyName, 5, "VOL", ColorSets::Normal);
	if (_ui.hexSpin(19, 5, _samplerState.volume)) {
		repatchSample();
	}

	_c.text(propertyName, 6, "PITCH", ColorSets::Normal);
	_ui.hexSpin(19, 6, _samplerState.pitch);

	_c.text(propertyName, 8, "FILTER", ColorSets::Normal);
	_ui.select<5>(19, 8, _samplerState.filter, { "NONE", "LOWP", "HIGHP", "BANDP", "ALLP" });

	_c.text(propertyName, 9, "CUTOFF", ColorSets::Normal);
	if (_ui.hexSpin(19, 9, _samplerState.cutoff)) {
		repatchSample();
	}

	_c.text(propertyName, 10, "Q", ColorSets::Normal);
	if (_ui.hexSpin(19, 10, _samplerState.q)) {
		repatchSample();
	}

	_c.fill(propertyName, 12, 13, 5, ColorSets::Shaded, 0);

	_ui.popColumn();
}
*/