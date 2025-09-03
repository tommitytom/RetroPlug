#include "SameBoyUtil.h"

#include <string_view>

extern "C" {
#include <gb.h>
}

#include "bootroms/agb_boot.h"
#include "bootroms/cgb_boot.h"
#include "bootroms/cgb_boot_fast.h"
#include "bootroms/cgb0_boot.h"
#include "bootroms/dmg_boot.h"
#include "bootroms/mgb_boot.h"
#include "bootroms/sgb_boot.h"
#include "bootroms/sgb2_boot.h"

#include "foundation/Math.h"
#include "foundation/Image.h"
#include "sameboy/Constants.h"
#include "sameboy/SameBoyComponents.h"

using namespace rp;

void SameBoyUtil::spinMs(GB_gameboy_t* gb, f32 ms) {
	spinNs(gb, ms * 1000000.0f);
}

void SameBoyUtil::spinNs(GB_gameboy_t* gb, f32 ns) {
	const f32 clockRate = (f32)GB_get_clock_rate(gb);

	while (ns > 0) {
		uint8 cycles = GB_run(gb);
		ns -= (f32)cycles * 1000000000.0f / 2.0f / clockRate;
	}
}

f32 SameBoyUtil::cyclesToNs(GB_gameboy_t* gb, uint64 cycles) {
	return (f32)cycles * 1000000000.0f / 2.0f / (f32)GB_get_clock_rate(gb);
}

f32 SameBoyUtil::cyclesToMs(GB_gameboy_t* gb, uint64 cycles) {
	return cyclesToNs(gb, cycles) / 1000000.0f;
}

const GB_model_t DEFAULT_GAMEBOY_MODEL = GB_model_t::GB_MODEL_CGB_C;

SameBoyState& getStateComponent(GB_gameboy_t* gb) {
	return *(SameBoyState*)GB_get_user_data(gb);
}

GB_model_t getGameboyModelId(GameboyModel model) {
	switch (model) {
		case GameboyModel::DmgB: return GB_model_t::GB_MODEL_DMG_B;
		case GameboyModel::CgbC: return GB_model_t::GB_MODEL_CGB_C;
		case GameboyModel::CgbE: return GB_model_t::GB_MODEL_CGB_E;
		case GameboyModel::Agb: return GB_model_t::GB_MODEL_AGB;
		default: return DEFAULT_GAMEBOY_MODEL;
	}
}

std::string_view findBootRom(GB_model_t model, bool fastBoot) {
	switch (model) {
		case GB_model_t::GB_MODEL_DMG_B: return std::string_view((const char*)dmg_boot, dmg_boot_len);
		case GB_model_t::GB_MODEL_AGB: return std::string_view((const char*)agb_boot, agb_boot_len);
		//case GameboyModel::SgbNtsc: return std::string_view((const char*)sgb_boot, sgb_boot_len);
		//case GameboyModel::SgbPal: return std::string_view((const char*)sgb_boot, sgb_boot_len);
		//case GameboyModel::Sgb2: return std::string_view((const char*)sgb2_boot, sgb2_boot_len);
		case GB_model_t::GB_MODEL_CGB_E:
		case GB_model_t::GB_MODEL_CGB_C:
		default:
			if (fastBoot) {
				return std::string_view((const char*)cgb_boot_fast, cgb_boot_fast_len);
			} else {
				return std::string_view((const char*)cgb_boot, cgb_boot_len);
			}
	}

	return std::string_view((const char*)cgb_boot, cgb_boot_len);
}

static uint32_t rgbEncode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b) {
	return 255 << 24 | b << 16 | g << 8 | r;
}

static void vblankHandler(GB_gameboy_t* gb, GB_vblank_type_t type) {
	if (type == GB_VBLANK_TYPE_NORMAL_FRAME) {
		SameBoyState& s = getStateComponent(gb);

		if (s.io) {
			if (!s.io->output.video) {
				s.io->output.video = std::make_shared<fw::Image>(PIXEL_WIDTH, PIXEL_HEIGHT);
			}

			s.io->output.video->write((const fw::Color4*)s.frameBuffer, PIXEL_COUNT);
		}
	}
}

static f32 s16ToF32(int16 source) {
	//return (((f32)source + 32768.0f) * 0.00003051804379339284f) - 1;
	//return (f32)source / 32768.0f;
	return source < 0 ? (f32)source / 32768.0f : (f32)source / 32767.0f;
}

static void audioHandler(GB_gameboy_t* gb, GB_sample_t* sample) {
	SameBoyState& s = getStateComponent(gb);

	//GB_sample_t smp =  gb->apu_output.current_sample[0];

	/*if (s.muteTimeout > 0) {
		sample->left = 0;
		sample->right = 0;
		s.muteTimeout--;
	}*/

	if (s.io) {
		fw::Float32Buffer* buffer = s.io->output.audio.get();
		f32* target = buffer->data();

		if (buffer) {
			if ((s.audioFrameCount + 1) * 2 <= buffer->size()) {
				target[s.audioFrameCount * 2] = s16ToF32(sample->left);
				target[s.audioFrameCount * 2 + 1] = s16ToF32(sample->right);

				//target[s.audioFrameCount * 2] = s16ToF32(smp.left * CH_STEP);
				//target[s.audioFrameCount * 2 + 1] = s16ToF32(smp.right * CH_STEP);
			} else {
				// Overflow!
				//spdlog::warn("Audio buffer overflow!");
				//std::cout << "Audio buffer overflow!" << std::endl;
			}
		} else {

		}
	}

	s.audioFrameCount++;
}

void loadBootRomHandler(GB_gameboy_t* gb, GB_boot_rom_t type) {
	const SameBoyState& s = getStateComponent(gb);
	GB_model_t model = getGameboyModelId(s.model);
	std::string_view bootRom = findBootRom(model, s.fastBoot);
	GB_load_boot_rom_from_buffer(gb, (const unsigned char*)bootRom.data(), bootRom.size());
}

void processButtons(const std::vector<fw::StreamButtonPress>& source, std::queue<OffsetButton>& target, f32 timeScale) {
	for (const fw::StreamButtonPress& press : source) {
		int offset = 0;
		if (target.size() > 0) {
			offset = target.back().offset + target.back().duration;
		}

		target.push(OffsetButton{
			.offset = offset,
			.duration = (int)(timeScale * press.duration),
			.button = (int)press.button,
			.down = press.down
		});
	}
}

void SameBoyUtil::process(SameBoyStateComponent** systems, size_t systemCount, uint32 sampleCount) {
	for (size_t i = 0; i < systemCount; ++i) {
		SameBoyState& s = *systems[i]->state;

		SystemIo::Input& input = s.io->input;
		const f32 timeScale = (f32)GB_get_sample_rate(s.gb) / 1000.0f;

		processButtons(input.buttons, s.buttonQueue, timeScale);

		while (s.buttonQueue.size() && s.buttonQueue.front().offset <= s.audioFrameCount) {
			OffsetButton b = s.buttonQueue.front();
			s.buttonQueue.pop();

			GB_set_key_state(s.gb, (GB_key_t)b.button, b.down);
		}

		while (s.audioFrameCount < sampleCount) {
			int ticks = GB_run(s.gb);
			//_state.linkTicksRemain -= ticks;
		}

		s.audioFrameCount = 0;

		const size_t buttonRemain = s.buttonQueue.size();

		for (size_t i = 0; i < buttonRemain; i++) {
			OffsetButton button = s.buttonQueue.front();
			button.offset -= s.audioFrameCount;
			s.buttonQueue.push(button);
			s.buttonQueue.pop();
		}
	}
}



static void serialStart(GB_gameboy_t* gb, bool bit_received) {}

static bool serialEnd(GB_gameboy_t* gb) { return true; }

bool SameBoyUtil::setup(const SameBoyComponent& comp, SameBoyState& state, uint32 sampleRate, const SystemLoadComponent& load) {
	const fw::Uint8Buffer* rom = load.findData("rom");
	if (!rom) {
		return false;
	}

	state.fastBoot = comp.fastBoot;

	state.gb = new GB_gameboy_t();
	GB_gameboy_t* gb = state.gb;

	GB_init(gb, getGameboyModelId(comp.model));
	GB_set_user_data(gb, &state);

	GB_set_sample_rate(gb, sampleRate);
	GB_set_pixels_output(gb, (uint32_t*)state.frameBuffer);

	GB_set_boot_rom_load_callback(gb, loadBootRomHandler);
	GB_set_rgb_encode_callback(gb, rgbEncode);
	GB_set_vblank_callback(gb, vblankHandler);
	GB_apu_set_sample_callback(gb, audioHandler);
	GB_set_serial_transfer_bit_start_callback(gb, serialStart);
	GB_set_serial_transfer_bit_end_callback(gb, serialEnd);

	GB_set_background_rendering_disabled(gb, false);
	GB_set_object_rendering_disabled(gb, false);

	GB_load_rom_from_buffer(gb, (const uint8_t*)rom->data(), rom->size());

	//GB_set_color_correction_mode(gb, GB_COLOR_CORRECTION_EMULATE_HARDWARE);
	GB_set_color_correction_mode(gb, GB_COLOR_CORRECTION_DISABLED);
	GB_set_highpass_filter_mode(gb, GB_HIGHPASS_ACCURATE);

	const fw::Uint8Buffer* sram = load.findData("sram");
	if (sram) {
		GB_load_battery_from_buffer(state.gb, (const uint8_t*)sram->data(), sram->size());
	}

	const fw::Uint8Buffer* stateBuffer = load.findData("state");
	if (stateBuffer) {
		if (GB_load_state_from_buffer(state.gb, stateBuffer->data(), stateBuffer->size()) != 0) {
			//std::cerr << "Failed to load state buffer" << std::endl;
		}
	}

	//_romName = GameboyUtil::getRomName((const char*)loadConfig.romBuffer->data());
	//SameBoyUtil::spinMs(gb, 500.0f); // Skip bootrom

	return true;
}

void SameBoyUtil::setRenderingDisabled(SameBoyState& state, bool disabled) {
	if (state.gb) {
		GB_set_background_rendering_disabled(state.gb, disabled);
		GB_set_object_rendering_disabled(state.gb, disabled);
	}
}

void SameBoyUtil::setSampleRate(SameBoyState& state, uint32 sampleRate) {
	if (state.gb) {
		GB_set_sample_rate(state.gb, sampleRate);
	}
}

void SameBoyUtil::setUserData(SameBoyState& state, void* userData) {
	if (state.gb) {
		GB_set_user_data(state.gb, userData);
	}
}

void SameBoyUtil::destroy(SameBoyState& state) {
	delete state.gb;
}
