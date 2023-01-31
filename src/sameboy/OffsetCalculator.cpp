#include "OffsetCalculator.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <memory>
#include <sstream>

#include "semver.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

extern "C" {
	#include <gb.h>
}

#include "bootroms/cgb_boot_fast.h"
#include "savs/offsetsav.h"
#include "savs/offsetsav2.h"

#include "lsdj/Sav.h"
#include "lsdj/LsdjUtil.h"

#include "foundation/FsUtil.h"
#include "util/GameboyUtil.h"

using namespace rp;
using namespace rp::lsdj;
using namespace rp::lsdj::OffsetCalculator;
namespace chrono = std::chrono;

struct Buffer {
	char* data = nullptr;
	size_t size;

	~Buffer() {
		if (data) {
			//delete[] data;
		}
	}
};

using BufferPtr = std::shared_ptr<Buffer>;

static void loadBootRomHandler(GB_gameboy_t* gb, GB_boot_rom_t type) {
	GB_load_boot_rom_from_buffer(gb, (const unsigned char*)cgb_boot_fast, cgb_boot_fast_len);
}

const fs::path DUMP_PATH = "C:\\code\\lsdjsite\\dump";

f64 cyclesToNanoseconds(GB_gameboy_t* gb, uint64 cycles) {
	return (f64)cycles * 1000000000.0 / 2.0 / (f64)GB_get_clock_rate(gb);
}

f64 nanoToMs(f64 nano) {
	return nano / 1000000.0;
}

f64 cyclesToMs(GB_gameboy_t* gb, uint64 cycles) {
	return nanoToMs(cyclesToNanoseconds(gb, cycles));
}

void spin(Context& ctx, f32 ms) {
	int64 nanoRemain = (int64)(ms * 1000000.0f);

	while (nanoRemain > 0) {
		uint8 cycles = GB_run(ctx.gb);
		nanoRemain -= (int64)cyclesToNanoseconds(ctx.gb, cycles);
	}
}

void dumpRam(Context& ctx, const fs::path& path) {
	size_t size;
	uint16 bank;
	void* data = GB_get_direct_access(ctx.gb, GB_DIRECT_ACCESS_RAM, &size, &bank);
	fw::FsUtil::writeFile(DUMP_PATH / path, (const char*)data, size);
}

BufferPtr dumpRam(Context& ctx) {
	size_t size;
	uint16 bank;
	void* data = GB_get_direct_access(ctx.gb, GB_DIRECT_ACCESS_RAM, &size, &bank);

	BufferPtr buffer = std::make_shared<Buffer>(Buffer{
		.data = new char[size],
		.size = size
	});

	memcpy(buffer->data, data, size);

	return buffer;
}

Buffer getRam(Context& ctx) {
	size_t size;
	uint16 bank;
	void* data = GB_get_direct_access(ctx.gb, GB_DIRECT_ACCESS_RAM, &size, &bank);

	return Buffer{ (char*)data, size };
}

void dumpFb(Context& ctx, const fs::path& path) {
	std::string p = (DUMP_PATH / path).string();
	stbi_write_png(p.c_str(), 160, 144, 4, ctx.frameBuffer, 160 * 4);
}

static void vblankHandler(GB_gameboy_t* gb) {}

static void audioHandler(GB_gameboy_t* gb, GB_sample_t* sample) {
	Context* ctx = (Context*)GB_get_user_data(gb);
	ctx->hadAudio = ctx->hadAudio || sample->left != 0;
}

static uint32_t rgbEncode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b) {
	return 255 << 24 | b << 16 | g << 8 | r;
}

void press(Context& ctx, GB_key_t button, f32 durationMs) {
	GB_set_key_state(ctx.gb, button, true);
	spin(ctx, durationMs / 2);
	GB_set_key_state(ctx.gb, button, false);
	spin(ctx, durationMs / 2);
}

void pressCombo(Context& ctx, std::vector<GB_key_t> buttons, size_t durationMs = 50) {
	for (GB_key_t button : buttons) {
		GB_set_key_state(ctx.gb, button, true);
		spin(ctx, durationMs);
	}

	for (GB_key_t button : buttons) {
		GB_set_key_state(ctx.gb, button, false);
	}

	spin(ctx, durationMs);
}

std::vector<size_t> findOffsets(const BufferPtr& source, const std::vector<uint8>& pattern, size_t startOffset = 0, size_t size = 0) {
	if (size == 0) {
		size = source->size - startOffset;
	}

	std::vector<size_t> offsets;
	for (size_t i = startOffset; i < startOffset + size; ++i) {
		if (memcmp(source->data + i, pattern.data(), pattern.size()) == 0) {
			offsets.push_back(i);
		}
	}

	return offsets;
}

void filterOffsets(const BufferPtr& source, std::vector<size_t>& offsets, const std::vector<uint8>& pattern) {
	std::vector<size_t> removals;

	for (size_t i = 0; i < offsets.size(); ++i) {
		const uint8* data = (const uint8*)source->data + offsets[i];

		if (memcmp(data, pattern.data(), pattern.size()) != 0) {
			removals.push_back(i);
		}
	}

	for (int32 i = (int32)removals.size() - 1; i >= 0; i--) {
		offsets.erase(offsets.begin() + removals[i]);
	}
}

int printCount = 0;

void printAt(const BufferPtr& buffer, size_t offset, size_t count) {
	std::cout << printCount++ << ": " << "[ ";

	for (size_t i = offset; i < offset + count; ++i) {
		std::cout << "0x" << std::hex << std::setfill('0') << std::setw(2) << (int)buffer->data[i];

		if (i != (offset + count) - 1) {
			std::cout << ",";
		}

		std::cout << " ";
	}

	std::cout << "]" << std::endl;
}

void initialize(Context& ctx, const char* romData) {
	ctx.gb = new GB_gameboy_t();

	GB_init(ctx.gb, GB_model_t::GB_MODEL_CGB_E);

	GB_set_user_data(ctx.gb, &ctx);

	GB_set_sample_rate(ctx.gb, 48000);
	GB_set_pixels_output(ctx.gb, ctx.frameBuffer);

	GB_set_boot_rom_load_callback(ctx.gb, loadBootRomHandler);
	GB_set_rgb_encode_callback(ctx.gb, rgbEncode);
	GB_set_vblank_callback(ctx.gb, vblankHandler);
	GB_apu_set_sample_callback(ctx.gb, audioHandler);

	GB_set_color_correction_mode(ctx.gb, GB_COLOR_CORRECTION_DISABLED);
	GB_set_highpass_filter_mode(ctx.gb, GB_HIGHPASS_OFF);

	GB_load_rom_from_buffer(ctx.gb, (const uint8_t*)romData, 1048576);
	GB_set_rendering_disabled(ctx.gb, true);

	if (ctx.semver < semver::version{ 9, 1, 4 }) {
		GB_load_battery_from_buffer(ctx.gb, (const uint8_t*)offsetsav, offsetsav_len);
	} else {
		GB_load_battery_from_buffer(ctx.gb, (const uint8_t*)offsetsav2, offsetsav2_len);
	}

	// Skip the bootloader
	spin(ctx, 500);
}

bool waitForAudio(Context& ctx, f32 timeoutMs) {
	ctx.hadAudio = false;
	int64 timeoutNano = (int64)(timeoutMs * 1000000.0f);

	uint64 count = 0;
	while (!ctx.hadAudio) {
		count += GB_run(ctx.gb);

		if ((int64)cyclesToNanoseconds(ctx.gb, count) > timeoutNano) {
			return false;
		}
	}

	return true;
}

f32 DEFAULT_DELAY = 39.0f;

f32 getStartDelay(semver::version v, const std::string& tags) {
	if (v == semver::version{ 7, 0, 0 }) {
		return 39.0f;
	}

	return DEFAULT_DELAY;
}

bool getBakedOffsets(Context& ctx) {
	if (ctx.semver == semver::version{ 7,0,0 }) {
		for (uint32 i = 0; i < 5; ++i) {
			ctx.offsets.channels[i].active = 232 + i;
			ctx.offsets.channels[i].phrasePosition = 364 + i;
			ctx.offsets.channels[i].chainPosition = 380 + i;
			ctx.offsets.channels[i].songPosition = 512 + i;
		}

		ctx.offsets.cursorX = 887;
		ctx.offsets.cursorY = 888;

		return true;
	}

	return false;
}

bool getOffsets(Context& ctx) {
	//auto startTime = std::chrono::high_resolution_clock::now();

	if (getBakedOffsets(ctx)) {
		return true;
	}

	/*size_t activeOffset = 0x0E8;
	size_t songOffset = 0x200;
	size_t chainOffset = 0x17C;
	size_t phraseOffset = 0x16C;
	size_t screenOffset = 0x334;

	size_t tempoOffset = 0x4B4;*/

	BufferPtr startDump = dumpRam(ctx);
	std::vector<BufferPtr> phraseDumps;
	std::vector<BufferPtr> chainDumps;
	std::vector<BufferPtr> songDumps;

	std::vector<BufferPtr> screenDumps;
	std::vector<BufferPtr> cursorDumps;

	GB_set_key_state(ctx.gb, GB_KEY_START, true);

	if (!waitForAudio(ctx, 2000.0f)) {
		dumpFb(ctx, ctx.version + ".png");
		std::cout << ctx.version << ": Failed to wait for audio" << std::endl;
		return false;
	}

	GB_set_key_state(ctx.gb, GB_KEY_START, false);

	spin(ctx, getStartDelay(ctx.semver, ctx.tags));

	//uint8 value = 0xFF;
	//uint64 cycles = 0;

	//size_t changeCount = 0;

	/*while (changeCount < 32) {
		Buffer buf = getRam(ctx);
		uint8 v = buf.data[364];
		if (v != value) {
			std::cout << changeCount << " :: " << (uint32)value << "->" << (uint32)v << " - " << cycles << ", " << nanoToMs(cyclesToNanoseconds(ctx.gb, cycles)) << std::endl;
			value = v;
			changeCount++;
			cycles = 0;
		}

		cycles += GB_run(ctx.gb);
	}*/

	const f32 PHRASE_STEP_SIZE = 32.0f;
	const f32 CHAIN_STEP_SIZE = PHRASE_STEP_SIZE * 16;

	for (size_t i = 0; i < 15; ++i) {
		phraseDumps.push_back(dumpRam(ctx));
		//printAt(phraseDumps.back(), 364, 4);
		spin(ctx, PHRASE_STEP_SIZE);
	}

	//std::cout << "=====" << std::endl;

	chainDumps.push_back(phraseDumps.front());
	songDumps.push_back(phraseDumps.front());

	for (size_t i = 0; i < 3; ++i) {
		chainDumps.push_back(dumpRam(ctx));
		//printAt(chainDumps.back(), 0x17C, 4);
		spin(ctx, CHAIN_STEP_SIZE);
	}

	songDumps.push_back(dumpRam(ctx));

	// Stop playing
	press(ctx, GB_KEY_START, 40.0f);

	BufferPtr stoppedDump = dumpRam(ctx);

	// Change screens
	pressCombo(ctx, { GB_KEY_SELECT, GB_KEY_UP });
	screenDumps.push_back(dumpRam(ctx));
	pressCombo(ctx, { GB_KEY_SELECT, GB_KEY_RIGHT });
	screenDumps.push_back(dumpRam(ctx));
	pressCombo(ctx, { GB_KEY_SELECT, GB_KEY_DOWN });
	screenDumps.push_back(dumpRam(ctx));
	pressCombo(ctx, { GB_KEY_SELECT, GB_KEY_DOWN });
	screenDumps.push_back(dumpRam(ctx));
	pressCombo(ctx, { GB_KEY_SELECT, GB_KEY_UP });
	pressCombo(ctx, { GB_KEY_SELECT, GB_KEY_LEFT });

	// Move the cursor to a different row and collumn

	press(ctx, GB_KEY_DOWN, 40.0f);
	press(ctx, GB_KEY_RIGHT, 40.0f);
	cursorDumps.push_back(dumpRam(ctx));
	//dumpFb(ctx, "cursor1.png");
	press(ctx, GB_KEY_DOWN, 40.0f);
	press(ctx, GB_KEY_RIGHT, 40.0f);
	cursorDumps.push_back(dumpRam(ctx));
	//dumpFb(ctx, "cursor2.png");
	press(ctx, GB_KEY_DOWN, 40.0f);
	press(ctx, GB_KEY_RIGHT, 40.0f);
	cursorDumps.push_back(dumpRam(ctx));
	//dumpFb(ctx, "cursor3.png");

	// Start playing again
	press(ctx, GB_KEY_START, 40.0f);
	spin(ctx, 10);

	songDumps.push_back(dumpRam(ctx));

	size_t OFFSET_SEARCH_SIZE = 0x400;
	if (ctx.semver >= semver::version{ 8, 2, 1 }) {
		OFFSET_SEARCH_SIZE = 0x500;
	}

	std::vector<size_t> startZeroOffsets = findOffsets(startDump, { 0x00, 0x00, 0x00, 0x00 }, 0, OFFSET_SEARCH_SIZE);
	std::vector<size_t> startSetOffsets = findOffsets(startDump, { 0xFF, 0xFF, 0xFF, 0xFF }, 0, OFFSET_SEARCH_SIZE);

	std::vector<size_t> cursorOffsets = startZeroOffsets;
	std::vector<size_t> activeOffsets = startZeroOffsets;
	std::vector<size_t> screenOffsets = findOffsets(startDump, { 0x00, 0x00 }, 0, OFFSET_SEARCH_SIZE);;
	std::vector<size_t> phraseOffsets = startSetOffsets;
	std::vector<size_t> chainOffsets = startSetOffsets;
	std::vector<size_t> songOffsets = startSetOffsets;

	filterOffsets(screenDumps[0], screenOffsets, { 0x00, 0xFF });
	filterOffsets(screenDumps[1], screenOffsets, { 0x01, 0xFF });
	filterOffsets(screenDumps[2], screenOffsets, { 0x01, 0x00 });
	filterOffsets(screenDumps[3], screenOffsets, { 0x01, 0x01 });

	for (uint8 i = 0; i < cursorDumps.size(); ++i) {
		filterOffsets(cursorDumps[i], cursorOffsets, { (uint8)(i + 1), (uint8)(i + 1) });
	}

	for (uint8 i = 0; i < phraseDumps.size(); ++i) {
		filterOffsets(phraseDumps[i], phraseOffsets, { (uint8)(i + 1), (uint8)(i + 1), (uint8)(i + 1), (uint8)(i + 1) });
		filterOffsets(phraseDumps[i], activeOffsets, { 0x01, 0x01, 0x01, 0x01 });
	}

	for (uint8 i = 0; i < chainDumps.size(); ++i) {
		filterOffsets(chainDumps[i], chainOffsets, { i, i, i, i });
		filterOffsets(chainDumps[i], activeOffsets, { 0x01, 0x01, 0x01, 0x01 });
	}

	filterOffsets(songDumps[0], songOffsets, { 0x00, 0x00, 0x00, 0x00 });
	filterOffsets(songDumps[1], songOffsets, { 0x01, 0x01, 0x01, 0x01 });
	filterOffsets(songDumps[2], songOffsets, { 0x03, 0x03, 0x03, 0x03 });

	filterOffsets(stoppedDump, activeOffsets, { 0x00, 0x00, 0x00, 0x00 });

	//auto endTime = std::chrono::high_resolution_clock::now();

	//auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(endTime - startTime);
	//std::cout << "TIME: " << duration.count() << std::endl;

	std::vector<std::string_view> fail;

	if (activeOffsets.size() != 1) {
		fail.push_back("active");
	}

	if (songOffsets.size() != 1) {
		fail.push_back("song");
	}

	if (chainOffsets.size() != 1) {
		fail.push_back("chain");
	}

	if (phraseOffsets.size() != 1) {
		fail.push_back("phrase");
	}

	if (cursorOffsets.size() != 1) {
		fail.push_back("cursor");
	}

	if (screenOffsets.size() != 1) {
		fail.push_back("screen");
	}

	if (fail.size() > 0) {
		std::cout << ctx.version << ": Failed to get offsets: ";

		for (size_t i = 0; i < fail.size(); ++i) {
			if (i != 0) {
				std::cout << ", ";
			}

			std::cout << fail[i];
		}

		std::cout << std::endl;

		return false;
	}

	for (size_t i = 0; i < 4; ++i) {
		ctx.offsets.channels[i].active = activeOffsets[0] + i;
		ctx.offsets.channels[i].songPosition = songOffsets[0] + i;
		ctx.offsets.channels[i].chainPosition = chainOffsets[0] + i;
		ctx.offsets.channels[i].phrasePosition = phraseOffsets[0] + i;
	}

	ctx.offsets.cursorX = cursorOffsets[0];
	ctx.offsets.cursorY = cursorOffsets[0] + 1;

	ctx.offsets.screenX = screenOffsets[0];
	ctx.offsets.screenY = screenOffsets[0] + 1;

	return true;
}

bool OffsetCalculator::calculate(const char* romData, Context& ctx, const std::string& filename) {
	ctx.filename = filename;
	ctx.romName = GameboyUtil::getRomName(romData);
	if (!LsdjUtil::getVersionFromName(ctx.romName, ctx.version)) {
		if (filename.size() > 0) {
			LsdjUtil::getVersionFromFilename(filename, ctx.version);
		}
	}

	if (filename.size()) {
		size_t tagIdx = filename.find_first_of("-");
		if (tagIdx != std::string::npos) {
			size_t dotIdx = filename.find_last_of(".");
			tagIdx++;

			ctx.tags = filename.substr(tagIdx, dotIdx - tagIdx);
		}
	}

	if (ctx.version.size() > 0) {
		ctx.semver = LsdjUtil::getSemVerFromVersion(ctx.version);
	}

	initialize(ctx, romData);

	bool valid = getOffsets(ctx);

	delete ctx.gb;
	ctx.gb = nullptr;

	return valid;
}

