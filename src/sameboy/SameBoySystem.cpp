#include "SameBoySystem.h"

#include <string_view>
#include <assert.h>
#include <iostream>

extern "C" {
	#include <gb.h>
	#include "SectionOffsetCollector.h"
}

#include "bootroms/agb_boot.h"
#include "bootroms/cgb_boot.h"
#include "bootroms/cgb_boot_fast.h"
#include "bootroms/dmg_boot.h"
#include "bootroms/sgb_boot.h"
#include "bootroms/sgb2_boot.h"

#include "util/GameboyUtil.h"

using namespace rp;

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
			return std::string_view((const char*)cgb_boot_fast, cgb_boot_fast_len);
		} else {
			return std::string_view((const char*)cgb_boot, cgb_boot_len);
		}
	}

	return std::string_view((const char*)cgb_boot, cgb_boot_len);
}

static void loadBootRomHandler(GB_gameboy_t* gb, GB_boot_rom_t type) {
	SameBoySystem::State* s = (SameBoySystem::State*)GB_get_user_data(gb);
	std::string_view bootRom = findBootRom(s->model, s->fastBoot);
	GB_load_boot_rom_from_buffer(gb, (const unsigned char*)bootRom.data(), bootRom.size());
}

static uint32_t rgbEncode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b) {
	return 255 << 24 | b << 16 | g << 8 | r;
}

static void vblankHandler(GB_gameboy_t* gb) {
	SameBoySystem::State* s = (SameBoySystem::State*)GB_get_user_data(gb);

	if (s->io) {
		if (!s->io->output.video) {
			s->io->output.video = std::make_shared<fw::Image>(PIXEL_WIDTH, PIXEL_HEIGHT);
		}

		s->io->output.video->write((const fw::Color4*)s->frameBuffer, PIXEL_COUNT);
	}
}

static f32 s16ToF32(int16 source) {
	f32 x = (f32)source;
	return ((x + 32768.0f) * 0.00003051804379339284f) - 1;
}

static void audioHandler(GB_gameboy_t* gb, GB_sample_t* sample) {
	SameBoySystem::State* s = (SameBoySystem::State*)GB_get_user_data(gb);

	//GB_sample_t smp =  gb->apu_output.current_sample[0];

	if (s->io) {
		fw::Float32Buffer* buffer = s->io->output.audio.get();

		if (buffer) {
			if ((s->audioFrameCount + 1) * 2 <= buffer->size()) {
				f32* target = buffer->data();
				target[s->audioFrameCount * 2] = s16ToF32(sample->left);
				target[s->audioFrameCount * 2 + 1] = s16ToF32(sample->right);

				//target[s->audioFrameCount * 2] = s16ToF32(smp.left * CH_STEP);
				//target[s->audioFrameCount * 2 + 1] = s16ToF32(smp.right * CH_STEP);
			} else {
				// Overflow!
				//spdlog::warn("Audio buffer overflow!");
				//std::cout << "Audio buffer overflow!" << std::endl;
			}
		}
	}

	s->audioFrameCount++;
}

SameBoySystem::SameBoySystem(SystemId id): System<SameBoySystem>(id) {
	_resolution = fw::DimensionT<uint32>(160, 144);
}

SameBoySystem::~SameBoySystem() {
	destroy();
}

MemoryAccessor SameBoySystem::getMemory(MemoryType type, AccessType access) {
	GB_direct_access_t sameboyType;
	bool found = false;

	switch (type) {
	case MemoryType::Rom: sameboyType = GB_DIRECT_ACCESS_ROM; found = true; break;
	case MemoryType::Ram: sameboyType = GB_DIRECT_ACCESS_RAM; found = true; break;
	case MemoryType::Sram: sameboyType = GB_DIRECT_ACCESS_CART_RAM; found = true; break;
	case MemoryType::Unknown: break;
	}

	if (found) {
		size_t memSize;
		uint16 memBank;
		void* memData = GB_get_direct_access(_state.gb, sameboyType, &memSize, &memBank);
		return MemoryAccessor(type, fw::Uint8Buffer((uint8*)memData, memSize, false), 0, nullptr);
	}

	return MemoryAccessor();
}

bool SameBoySystem::load(LoadConfig&& loadConfig) {
	if (loadConfig.romBuffer) {
		if (!_state.gb) {
			_state.gb = new GB_gameboy_t();

			GB_init(_state.gb, getGameboyModelId(_state.model));
			GB_set_user_data(_state.gb, &_state);

			GB_set_sample_rate(_state.gb, _sampleRate);
			GB_set_pixels_output(_state.gb, (uint32_t*)_state.frameBuffer);

			GB_set_boot_rom_load_callback(_state.gb, loadBootRomHandler);
			GB_set_rgb_encode_callback(_state.gb, rgbEncode);
			GB_set_vblank_callback(_state.gb, vblankHandler);
			GB_apu_set_sample_callback(_state.gb, audioHandler);
			//GB_set_serial_transfer_bit_start_callback(_state.gb, serialStart);
			//GB_set_serial_transfer_bit_end_callback(_state.gb, serialEnd);

			//GB_set_color_correction_mode(_state.gb, GB_COLOR_CORRECTION_EMULATE_HARDWARE);
			GB_set_color_correction_mode(_state.gb, GB_COLOR_CORRECTION_DISABLED);
			GB_set_highpass_filter_mode(_state.gb, GB_HIGHPASS_ACCURATE);
		}

		GB_load_rom_from_buffer(_state.gb, (const uint8_t*)loadConfig.romBuffer->data(), loadConfig.romBuffer->size());

		_romName = GameboyUtil::getRomName((const char*)loadConfig.romBuffer->data());
	}

	if (loadConfig.sramBuffer) {
		GB_load_battery_from_buffer(_state.gb, (const uint8_t*)loadConfig.sramBuffer->data(), loadConfig.sramBuffer->size());
	}

	if (loadConfig.stateBuffer) {
		if (GB_load_state_from_buffer(_state.gb, loadConfig.stateBuffer->data(), loadConfig.stateBuffer->size()) != 0) {
			std::cerr << "Failed to load state buffer" << std::endl;
		}
	}

	if (loadConfig.reset) {
		GB_reset(_state.gb);
	}

	return false; 
}

void SameBoySystem::reset() {
	GB_reset(_state.gb);
}

void SameBoySystem::setSampleRate(uint32 sampleRate) {
	_sampleRate = sampleRate;
	GB_set_sample_rate(_state.gb, sampleRate);
}

bool SameBoySystem::saveState(fw::Uint8Buffer& target) {
	size_t size = GB_get_save_state_size(_state.gb);
	if (target.size() != size) {
		target.resize(size);
	}

	GB_save_state_to_buffer(_state.gb, target.data());

	return true;
}

void SameBoySystem::destroy() {
	if (_state.gb) {
		delete _state.gb;
		_state.gb = nullptr;
	}
}
