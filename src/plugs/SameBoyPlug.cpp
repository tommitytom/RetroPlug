#include "SameBoyPlug.h"

#include <fstream>

extern "C" {
	#include <gb.h>
}

#include "retroplug/Constants.h"
#include "retroplug/util/SampleConverter.h"
#include "generated/bootroms/agb_boot.h"
#include "generated/bootroms/cgb_boot.h"
#include "generated/bootroms/cgb_fast_boot.h"
#include "generated/bootroms/dmg_boot.h"
#include "generated/bootroms/sgb_boot.h"
#include "generated/bootroms/sgb2_boot.h"

const size_t LINK_TICKS_MAX = 3907;

const size_t MAX_SERIAL_ITEMS = 128;
const size_t MAX_BUTTON_ITEMS = 64;
const GB_model_t DEFAULT_GAMEBOY_MODEL = GB_model_t::GB_MODEL_CGB_C;

GB_model_t getGameboyModelId(GameboyModel model) {
	switch (model) {
		case GameboyModel::DmgB: return GB_model_t::GB_MODEL_DMG_B;
		case GameboyModel::CgbC: return GB_model_t::GB_MODEL_CGB_C;
		case GameboyModel::CgbE: return GB_model_t::GB_MODEL_CGB_E;
		case GameboyModel::Agb: return GB_model_t::GB_MODEL_AGB;
		default: return DEFAULT_GAMEBOY_MODEL;
	}
}

std::string_view findBootRom(GameboyModel model, bool fastBoot) {
	switch (model) {
		case GameboyModel::DmgB: return std::string_view((const char*)dmg_boot, dmg_boot_len);
		case GameboyModel::Agb: return std::string_view((const char*)agb_boot, agb_boot_len);
		//case GameboyModel::SgbNtsc: return std::string_view((const char*)sgb_boot, sgb_boot_len);
		//case GameboyModel::SgbPal: return std::string_view((const char*)sgb_boot, sgb_boot_len);
		//case GameboyModel::Sgb2: return std::string_view((const char*)sgb2_boot, sgb2_boot_len);
		case GameboyModel::CgbE:
		case GameboyModel::CgbC:
		default:
			if (fastBoot) {
				return std::string_view((const char*)cgb_fast_boot, cgb_fast_boot_len);
			} else {
				return std::string_view((const char*)cgb_boot, cgb_boot_len);
			}
	}

	return std::string_view((const char*)cgb_boot, cgb_boot_len);
}

SameBoyPlug::SameBoyPlug() {
	_dimensions.w = PIXEL_WIDTH;
	_dimensions.h = PIXEL_HEIGHT;

	//_audioScratchSize = 256;
	//_audioScratch = new int16_t[_audioScratchSize];
}

void SameBoyPlug::pressButtons(const StreamButtonPress* presses, size_t pressCount) {
	const double samplesPerMs = _sampleRate / 1000.0;

	for (size_t i = 0; i < pressCount; ++i) {
		int offset = 0;
		if (_state.buttonQueue.size() > 0) {
			offset = _state.buttonQueue.back().offset + _state.buttonQueue.back().duration;
		}

		_state.buttonQueue.push(OffsetButton {
			.offset = offset,
			.duration = (int)(samplesPerMs * presses[i].duration),
			.button = presses[i].button,
			.down = presses[i].down
		});
	}
}

static uint32_t rgbEncode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b) {
	return 255 << 24 | b << 16 | g << 8 | r;
}

static void vblankHandler(GB_gameboy_t* gb) {
	SameBoyPlugState* state = (SameBoyPlugState*)GB_get_user_data(gb);
	state->vblankOccurred = true;
}

static void audioHandler(GB_gameboy_t* gb, GB_sample_t* sample) {
	SameBoyPlugState* s = (SameBoyPlugState*)GB_get_user_data(gb);
	s->audioBuffer[s->currentAudioFrames].left = sample->left;
	s->audioBuffer[s->currentAudioFrames].right = sample->right;
	s->currentAudioFrames++;
}

static void serialStart(GB_gameboy_t* gb, bool bit_received) {
	SameBoyPlugState* s = (SameBoyPlugState*)GB_get_user_data(gb);
	s->bitToSend = bit_received;
}

static bool serialEnd(GB_gameboy_t* gb) {
	SameBoyPlugState* s = (SameBoyPlugState*)GB_get_user_data(gb);

	bool ret = s->linkTargets.size() > 0 ? GB_serial_get_data_bit(s->linkTargets[0]->gb) : true;

	for (SameBoyPlugState* linkTarget : s->linkTargets) {
		GB_serial_set_data_bit(linkTarget->gb, s->bitToSend);
	}

	return ret;
}

static void loadBootRomHandler(GB_gameboy_t* gb, GB_boot_rom_t type) {
	SameBoyPlugState* s = (SameBoyPlugState*)GB_get_user_data(gb);
	std::string_view bootRom = findBootRom(s->model, s->fastBoot);
	GB_load_boot_rom_from_buffer(gb, cgb_boot, cgb_boot_len);
}

void SameBoyPlug::init(GameboyModel model) {
	assert(_state.gb == nullptr);

	_state.gb = new GB_gameboy_t();

	GB_init(_state.gb, getGameboyModelId(model));

	GB_set_pixels_output(_state.gb, (uint32_t*)_state.frameBuffer);
	GB_set_sample_rate(_state.gb, 44100);
	GB_set_user_data(_state.gb, &_state);

	GB_set_boot_rom_load_callback(_state.gb, loadBootRomHandler);
	GB_set_rgb_encode_callback(_state.gb, rgbEncode);
	GB_set_vblank_callback(_state.gb, vblankHandler);
	GB_apu_set_sample_callback(_state.gb, audioHandler);
	GB_set_serial_transfer_bit_start_callback(_state.gb, serialStart);
	GB_set_serial_transfer_bit_end_callback(_state.gb, serialEnd);

	GB_set_color_correction_mode(_state.gb, GB_COLOR_CORRECTION_EMULATE_HARDWARE);
	GB_set_highpass_filter_mode(_state.gb, GB_HIGHPASS_ACCURATE);

	GB_set_rendering_disabled(_state.gb, true);
}

void SameBoyPlug::loadRom(const char* data, size_t size, const SameBoySettings& settings, bool fastBoot) {
	_settings = settings;

	_state.model = settings.model;
	_state.fastBoot = fastBoot;

	init(settings.model);

	GB_load_rom_from_buffer(_state.gb, (const uint8_t*)data, size);
	GB_set_rendering_disabled(_state.gb, false);

	_resetSamples = (int)(_sampleRate / 2);
}

void SameBoyPlug::reset(GameboyModel model, bool fastBoot) {
	_settings.model = model;

	_state.model = model;
	_state.fastBoot = fastBoot;

	GB_switch_model_and_reset(_state.gb, getGameboyModelId(model));

	_resetSamples = (int)(_sampleRate / 2);
}

void SameBoyPlug::setSampleRate(double sampleRate) {
	GB_set_sample_rate(_state.gb, (uint32_t)sampleRate);
	_sampleRate = sampleRate;
}

size_t SameBoyPlug::saveStateSize() {
	return (size_t)GB_get_save_state_size(_state.gb);
}

size_t SameBoyPlug::sramSize() {
	return (size_t)GB_save_battery_size(_state.gb);
}

size_t SameBoyPlug::saveSram(char* data, size_t size) {
	return (size_t)GB_save_battery_to_buffer(_state.gb, (uint8_t*)data, size);
}

bool SameBoyPlug::loadSram(const char* data, size_t size, bool reset) {
	GB_load_battery_from_buffer(_state.gb, (const uint8_t*)data, size);

	if (reset) {
		this->reset(_settings.model, true);
	}

	return true;
}

bool SameBoyPlug::clearBattery(bool reset) {
	size_t size = sramSize();
	std::vector<char> d(size);
	memset(d.data(), 0, size);

	loadSram(d.data(), size, reset);

	return true;
}

size_t SameBoyPlug::saveState(char* target, size_t size) {
	size_t stateSize = GB_get_save_state_size(_state.gb);
	if (size >= stateSize) {
		GB_save_state_to_buffer(_state.gb, (uint8_t*)target);
		return stateSize;
	}

	return 0;
}

void SameBoyPlug::loadState(const char* source, size_t size) {
	assert(GB_get_save_state_size(_state.gb) == size);
	GB_load_state_from_buffer(_state.gb, (const uint8_t*)source, size);
}

void SameBoyPlug::setSetting(const std::string& name, int value) {
	if (name == "Color Correction") {
		GB_set_color_correction_mode(_state.gb, (GB_color_correction_mode_t)value);
	} else if (name == "High-pass Filter") {
		GB_set_highpass_filter_mode(_state.gb, (GB_highpass_mode_t)value);
	}
}

void SameBoyPlug::setLinkTargets(std::vector<SameBoyPlugPtr> linkTargets) {
	_state.linkTargets.clear();

	for (size_t i = 0; i < linkTargets.size(); i++) {
		_state.linkTargets.push_back(linkTargets[i]->getState());
	}
}

void SameBoyPlug::sendSerialByte(int offset, int byte) {
	_state.serialQueue.push(OffsetByte {
		.offset = offset,
		.byte = (char)byte,
		.bitCount = 8
	});
}

// This is called from the audio thread
void SameBoyPlug::update(size_t audioFrames) {
	_state.vblankOccurred = false;

	int delta = 0;
	while (_state.currentAudioFrames < audioFrames) {
		// Send bytes to the link port if required
		if (_state.linkTicksRemain <= 0) {
			if (!_state.serialQueue.empty()) {
				OffsetByte b = _state.serialQueue.front();
				_state.serialQueue.pop();

				for (int i = b.bitCount - 1; i >= 0; i--) {
				//for (int i = 0; i < b.bitCount; i++) {
					bool bit = (bool)((b.byte & (1 << i)) >> i);
					GB_serial_set_data_bit(_state.gb, bit);
				}
			}

			_state.linkTicksRemain += LINK_TICKS_MAX;
		}

		// Send button presses if required
		while (!_state.buttonQueue.empty() && _state.buttonQueue.front().offset <= _state.currentAudioFrames) {
			OffsetButton b = _state.buttonQueue.front();
			_state.buttonQueue.pop();

			GB_set_key_state(_state.gb, (GB_key_t)b.button, b.down);
		}

		int ticks = GB_run(_state.gb);
		delta += ticks;
		_state.linkTicksRemain -= ticks;
	}

	size_t buttonRemain = _state.buttonQueue.size();
	for (size_t i = 0; i < buttonRemain; i++) {
		OffsetButton button = _state.buttonQueue.front();
		button.offset -= _state.currentAudioFrames;
		_state.buttonQueue.push(button);
		_state.buttonQueue.pop();
	}

	// If there are any serial/midi events that still haven't been processed, set their
	// offsets to 0 so they get processed immediately at the start of the next frame.
	size_t serialRemain = _state.serialQueue.size();
	for (size_t i = 0; i < serialRemain; i++) {
		OffsetByte b = _state.serialQueue.front();
		b.offset = 0;
		_state.serialQueue.push(b);
		_state.serialQueue.pop();
	}
	
	updateAV(audioFrames);
}

void SameBoyPlug::updateMultiple(SameBoyPlug** plugs, size_t plugCount, size_t audioFrames) {
	SameBoyPlugState* st[MAX_SYSTEMS];
    for (size_t i = 0; i < plugCount; i++) {
		st[i] = plugs[i]->getState();
        st[i]->vblankOccurred = false;
    }

    // TODO: Send button presses?  Use sameboy_update as a reference

    size_t complete = 0;
    while (complete != plugCount) {
        complete = 0;
        for (size_t i = 0; i < plugCount; i++) {
			SameBoyPlugState* s = st[i];

            if (s->currentAudioFrames < audioFrames) {
				// Send button presses if required
				while (!s->buttonQueue.empty() && s->buttonQueue.front().offset <= s->currentAudioFrames) {
					OffsetButton b = s->buttonQueue.front();
					s->buttonQueue.pop();

					GB_set_key_state(s->gb, (GB_key_t)b.button, b.down);
				}

                GB_run(s->gb);
            } else {
                complete++;
            }
        }
    }

	for (size_t i = 0; i < plugCount; i++) {
		plugs[i]->updateAV(audioFrames);
	}
}

void SameBoyPlug::disableRendering(bool disable) {
	GB_set_rendering_disabled(_state.gb, disable);
}

void SameBoyPlug::setRomData(DataBuffer<char>* data) {
	size_t size;
	uint16_t bank;
	void* rom = GB_get_direct_access(_state.gb, GB_DIRECT_ACCESS_ROM, &size, &bank);

	if (size <= data->size()) {
		memcpy(rom, data->data(), data->size());
	}
}

void SameBoyPlug::patchMemory(DirectAccessType::Enum memoryType, DataBuffer<char>* data, size_t offset) {
	size_t size;
	uint16_t bank;
	char* target = (char*)GB_get_direct_access(_state.gb, (GB_direct_access_t)memoryType, &size, &bank);

	if (offset + data->size() <= size) {
		memcpy(target + offset, data->data(), data->size());
	}
}

void SameBoyPlug::updateAV(int audioFrames) {
	int sampleCount = audioFrames * 2;
	/*if (sampleCount > _audioScratchSize) {
		if (_audioScratch) {
			delete[] _audioScratch;
		}

		std::cout << "Resizing to: " << sampleCount * 2 << std::endl;
		_audioScratch = new int16_t[sampleCount * 2];
		_audioScratchSize = sampleCount;
	}*/

	if (sampleCount <= AUDIO_SCRATCH_SIZE) {
		if (_resetSamples <= 0) {
			SampleConverter::s16_to_f32(_audioBuffer->data->data(), (int16_t*)_state.audioBuffer, sampleCount);
		} else {
			_audioBuffer->data->clear();
			_resetSamples -= audioFrames;
		}

		_state.currentAudioFrames = 0;
	} else {
		_audioBuffer->data->clear();
	}

	if (_videoBuffer->data.get()) {
		if (_state.vblankOccurred) {
			memcpy(_videoBuffer->data.get(), _state.frameBuffer, FRAME_BUFFER_SIZE);
			_videoBuffer->hasData = true;
		}
	}
}

void SameBoyPlug::shutdown() {
	if (_state.gb) {
		GB_free(_state.gb);
		delete _state.gb;
		_state.gb = nullptr;
	}

	if (_audioScratch) {
		delete[] _audioScratch;
		_audioScratch = nullptr;
		_audioScratchSize = 0;
	}
}
