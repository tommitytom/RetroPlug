#pragma once

#include <assert.h>
#include <array>
#include <spdlog/spdlog.h>

#include "util/DataBuffer.h"
#include "util/fs.h"
#include "core/MemoryAccessor.h"

namespace rp::lsdj {
	enum class ColorSets {
		Normal,
		Shaded,
		Alternate,
		Selection,
		Scroll
	};

	struct Palette {
		static const size_t COLOR_SET_SIZE = 4 * 2;  // one colorset contains 2 colors
		static const size_t COLOR_SET_COUNT = 5;  // one palette contains 5 color sets
		static const size_t NAME_SIZE = 4;
		static const size_t SIZE = COLOR_SET_COUNT * COLOR_SET_SIZE;

		struct ColorSet {
			Color3 first;
			Color3 second;
		};

		char name[NAME_SIZE] = { '\n' };
		ColorSet sets[COLOR_SET_COUNT];

		Color3 getColor(ColorSets colorSet, uint32 pixel) const {
			ColorSet set = sets[(int)colorSet];

			Color3 val;
			switch (pixel) {
			case 0:
				val = set.first;
				break;
			case 1:
				val = blendColors(set.first, set.second);
				break;
			case 2:
				val = set.second;
				break;
			default:
				val = set.first;
				break;
			}

			return val;
		}

	private:
		Color3 blendColors(Color3 color1, Color3 color2) const {
			return Color3{
				.r = (uint8)((color1.r + color2.r) / 2),
				.g = (uint8)((color1.g + color2.g) / 2),
				.b = (uint8)((color1.b + color2.b) / 2)
			};
		}
	};

	struct Font {
		static const size_t TILE_WIDTH = 8;
		static const size_t TILE_HEIGHT = 8;
		static const size_t TILE_COUNT = 71;
		static const size_t TILE_SIZE = 16;
		static const size_t NAME_SIZE = 4;
		static const size_t HEADER_SIZE = 130;
		static const size_t SIZE = 0xE96;

		struct Tile {
			uint8 pixels[TILE_WIDTH * TILE_HEIGHT];
		};

		char name[NAME_SIZE] = { '\n' };
		Tile tiles[TILE_COUNT];
	};

	namespace SampleUtil {
		const size_t SAMPLES_PER_BYTE_4BIT = 2;

		static void convertNibblesToF32(const Uint8Buffer& input, Float32Buffer& output) {
			output.resize(input.size() * SAMPLES_PER_BYTE_4BIT);

			for (size_t i = 0; i < input.size(); ++i) {
				uint8 n = input[i];
				output[i * 2] = (f32)((n & 0xF0) >> 4);
				output[i * 2 + 1] = (f32)((n & 0xF));
			}

			for (size_t i = 0; i < output.size(); ++i) {
				output[i] = (output[i] / 7.5f) - 1.0f;
			}
		}

		static void convertF32ToNibbles(const Float32Buffer& input, Uint8Buffer& output) {
			output.resize(input.size() / SAMPLES_PER_BYTE_4BIT);

			int offset = 0;
			size_t addedBytes = 0;
			int outputCounter = 0;
			uint8* outputBuffer = new uint8[32];

			outputBuffer[0] = 0;

			for (size_t i = 0; i < input.size(); ++i) {
				f32 s = (std::min(1.0f, std::max(-1.0f, input[i])) + 1.0f) * 0.5f;
				uint8 b = 0xf - ((uint8)(s * 0xf)); // TODO: Dither here?

				// Starting from LSDj 9.2.0, first sample is skipped to compensate for wave refresh bug.
				// This rotates the wave frame rightwards.
				outputBuffer[(outputCounter + 1) % 32] = b;

				if (outputCounter == 31) {
					for (int j = 0; j != 32; j += 2) {
						output[offset++] = (uint8)(outputBuffer[j] * 0x10 + outputBuffer[j + 1]);
					}

					outputCounter = -1;
					addedBytes += 0x10;
				}

				outputCounter++;
			}

			output.resize(addedBytes);
		}
	}

	struct Kit {
		static constexpr size_t MAX_SAMPLES = 15;
		static constexpr size_t MAX_SAMPLE_SPACE = 0x3fa0;
		static constexpr size_t NAME_OFFSET = 0x52;
		static constexpr size_t NAME_SIZE = 6;
		static constexpr size_t SAMPLE_NAME_OFFSET = 0x22;
		static constexpr size_t SAMPLE_NAME_SIZE = 3;
		static constexpr size_t SAMPLE_DATA_OFFSET = 0x4000 - MAX_SAMPLE_SPACE;

		// 16x offsets, uint16, offset by 0x4000, first is always 0
		// 15x sample names, 3 bytes each
		// name, 6 bytes
		// sample data, 0x3fa0 bytes

		MemoryAccessor kitData;
		int32 _idx;

		Kit() {}
		Kit(MemoryAccessor _kitData, int32 idx): kitData(_kitData), _idx(idx) {}

		int32 getIndex() const {
			return _idx;
		}

		bool isValid() const {
			return kitData.isValid() && _idx != -1;
		}

		const Uint8Buffer& getBuffer() const {
			return kitData.getBuffer();
		}

		std::string_view getName() const {
			return std::string_view((const char*)kitData.getData() + Kit::NAME_OFFSET, Kit::NAME_SIZE);
		}

		std::string_view getSampleName(size_t sampleIdx) const {
			size_t nameOffset = getSampleNameOffset(sampleIdx);

			if (kitData[nameOffset] != 0) {
				return std::string_view((const char*)kitData.getData() + nameOffset, 3);
			}

			return std::string_view("N/A");
		}

		const Uint8Buffer getSampleData(size_t sampleIdx) const {
			size_t nameOffset = getSampleNameOffset(sampleIdx);

			if (kitData[nameOffset] != 0) {
				size_t offset = sampleIdx * 2;
				size_t start = (0xFF & kitData[offset]) | ((0xFF & kitData[offset + 1]) << 8);
				size_t stop = (0xFF & kitData[offset + 2]) | ((0xFF & kitData[offset + 3]) << 8);

				spdlog::info("{}, {} :: {}, {}, {}, {}", start - 0x4000, stop - 0x4000, (uint32)kitData[offset], (uint32)kitData[offset + 1], (uint32)kitData[offset + 2], (uint32)kitData[offset + 3]);

				if (stop > start) {
					return kitData.getBuffer().slice(start - 0x4000, stop - start);
				}
			}

			return Uint8Buffer();
		}

		int32 addSample(std::string_view name, const Uint8Buffer& data) {
			size_t sampleIdx;
			uint16 offset;

			if (!findEmptySample(sampleIdx, offset)) {
				spdlog::warn("Failed to add sample - no sample slots left!");
				return -1;
			}

			if ((uint32)data.size() > 0x4000u - (offset - 0x4000u)) {
				spdlog::warn("Failed to add sample - not enough space in kit!");
				return -1;
			}

			const size_t KIT_OFFSET_SIZE = sizeof(uint16);

			// Write offset
			uint16 nextOffset = offset + (uint16)data.size();
			kitData.write((sampleIdx + 1) * KIT_OFFSET_SIZE, nextOffset);

			setSampleName(sampleIdx, name);

			// Write sample data
			kitData.write(offset - 0x4000, data);

			fsutil::writeFile("C:/temp/test.kit", (const char*)kitData.getData(), 0x4000);

			return (int32)sampleIdx;
		}

		void setSampleName(size_t sampleIdx, std::string_view name) {
			size_t nameOffset = getSampleNameOffset(sampleIdx);

			for (size_t i = 0; i < SAMPLE_NAME_SIZE; ++i) {
				char v = i < name.size() ? (char)toupper(name[i]) : '-';
				kitData.set(nameOffset + i, (uint8)v);
			}
		}

		bool findEmptySample(size_t& sampleIdx, uint16& offset, size_t sampleOffset = 0) {
			uint16 lastOffset = 0x4060;
			const uint16* offsets = (const uint16*)kitData.getData();

			for (size_t i = sampleOffset + 1; i <= MAX_SAMPLES; ++i) {
				if (offsets[i] == 0) {
					sampleIdx = i - 1;
					offset = lastOffset;
					return true;
				} else {
					lastOffset = offsets[i];
				}
			}

			return false;
		}

		bool setSampleData(size_t sampleIdx, const Uint8Buffer& data) {
			if (data.size() == getSampleDataLength(sampleIdx)) {
				// No need to rebuild offset table
				size_t offset = getSampleOffset(sampleIdx) - 0x4000;
				kitData.write(offset, data);
				return true;
			}

			return false;
		}

		size_t getSampleDataLength(size_t sampleIdx) {
			size_t nameOffset = getSampleNameOffset(sampleIdx);

			if (kitData[nameOffset] != 0) {
				size_t offset = sampleIdx * 2;
				size_t start = (0xFF & kitData[offset]) | ((0xFF & kitData[offset + 1]) << 8);
				size_t stop = (0xFF & kitData[offset + 2]) | ((0xFF & kitData[offset + 3]) << 8);
				return stop - start;
			}

			return 0;
		}

		size_t getSampleOffset(size_t sampleIdx) const {
			size_t offset = sampleIdx * 2;
			return (0xFF & kitData[offset]) | ((0xFF & kitData[offset + 1]) << 8);
		}

		size_t getRemainingData() const {
			return MAX_SAMPLE_SPACE;
		}

	private:
		size_t getSampleNameOffset(size_t sampleIdx) const {
			return Kit::SAMPLE_NAME_OFFSET + sampleIdx * 3;
		}
	};

	class Rom {
	private:
		MemoryAccessor _romData;

		const uint8* _paletteNames = nullptr;
		const uint8* _fontNames = nullptr;
		const uint8* _paletteData = nullptr;
		const uint8* _fontData = nullptr;

		const std::array<uint8, 30> NAME_CHECK = { 0x47, 0x52, 0x41, 0x59, 0, 0x49, 0x4E, 0x56, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		const std::array<uint8, 20> PALETTE_CHECK = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x48, 0x48, 0x48 };
		const std::array<uint8, 16> FONT_CHECK = { 0, 0, 0, 0, 0xD0, 0x90, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0xD0, 0x90, 0, 0 };
		const std::array<uint8, 6> VERSION_CHECK = { 0x4C, 0x53, 0x44, 0x6A, 0x2D, 0x76 };

	public:
		static const size_t ROM_SIZE = 0x100000;

		static const size_t BANK_COUNT = 64;
		static const size_t BANK_SIZE = 0x4000;

		static const size_t PALETTE_COUNT = 6;

		static const size_t FONT_COUNT = 3;
		static const size_t ALTERNATE_FONT_OFFSET = 0x4D2;

		static const size_t KIT_COUNT = 51;
		const std::array<size_t, 51> KIT_LOOKUP = { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63 };

		Rom() {}
		Rom(MemoryAccessor romData) : _romData(romData) { updateOffsets(); }

		Rom& operator=(const Rom& other) {
			_romData = other._romData;
			updateOffsets();
			return *this;
		}

		bool isValid() const {
			return _romData.isValid();
		}

		const uint8* getBankData(size_t idx) const {
			assert(idx < BANK_COUNT);
			return _romData.getData() + idx * BANK_SIZE;
		}

		MemoryAccessor getBankAccessor(size_t idx) {
			return _romData.slice(idx * BANK_SIZE, BANK_SIZE);
		}

		MemoryAccessor& getAccessor() {
			return _romData;
		}

		template <const size_t DataSize>
		int32 findOffset(size_t bankIdx, const std::array<uint8, DataSize>& data, int32 offset) const {
			size_t bankOffset = bankIdx * BANK_SIZE;
			const uint8* bankData = _romData.getData() + bankOffset;

			for (size_t i = 0; i < BANK_SIZE; ++i) {
				if (memcmp(bankData + i, data.data(), data.size()) == 0) {
					return (int32)(bankOffset + i) + offset;
				}
			}

			return -1;
		}

		void updateOffsets() {
			int32 version = findOffset(0, VERSION_CHECK, 6);
			int32 names = findOffset(27, NAME_CHECK, 0);
			int32 palettes = findOffset(1, PALETTE_CHECK, -((int32)PALETTE_COUNT * (int32)Palette::SIZE));
			int32 fonts = findOffset(30, FONT_CHECK, 16);

			assert(version != -1);
			assert(names != -1);
			assert(palettes != -1);
			assert(fonts != -1);

			const uint8* d = _romData.getData();

			_paletteNames = d + names + 30;
			_paletteData = d + palettes;
			_fontNames = d + names - 15;
			_fontData = d + fonts;
		}

		Color3 unpackColor(const uint8* data) const {
			Color3 col = {
				.r = (uint8)(data[0] & 0x1F),
				.g = (uint8)(((data[1] & 3) << 3) | ((data[0] & 0xE0) >> 5)),
				.b = (uint8)(data[1] >> 2)
			};

			return Color3 {
				.r = (uint8)(((col.r << 3) * 255) / 0xF8),
				.g = (uint8)(((col.g << 3) * 255) / 0xF8),
				.b = (uint8)(((col.b << 3) * 255) / 0xF8),
			};
		}

		uint8 getTilePixel(const uint8* data, uint32 x, uint32 y) const {
			uint32 pixelOffset = y * 2;
			uint32 xMask = 7 - x;

			uint8 value = (data[pixelOffset] >> xMask) & 1;
			value |= ((data[pixelOffset + 1] >> xMask) & 1) << 1;

			if (value == 3) {
				value = 2;
			}

			return value;
		}

		bool kitIsEmpty(size_t idx) const {
			const uint8* bankData = getBankData(KIT_LOOKUP[idx]);
			return bankIsEmptyKit(bankData);
		}

		void setKit(size_t idx, const Uint8Buffer& data) {

		}

		Kit getKit(size_t idx) {
			size_t bankIdx = KIT_LOOKUP[idx];
			return Kit(getBankAccessor(bankIdx), (int32)idx);
		}


		std::string_view getKitName(size_t idx) const {
			size_t bankIdx = KIT_LOOKUP[idx];
			const char* kitData = (const char*)getBankData(bankIdx);
			return std::string_view(kitData + Kit::NAME_OFFSET, Kit::NAME_SIZE);
		}

		std::string_view getKitSampleName(size_t kitIdx, size_t sampleIdx) const {
			size_t bankIdx = KIT_LOOKUP[kitIdx];
			const char* kitData = (const char*)getBankData(bankIdx);

			size_t nameOffset = Kit::SAMPLE_NAME_OFFSET + sampleIdx * 3;
			if (kitData[nameOffset] != 0) {
				return std::string_view(kitData + nameOffset, 3);
			}

			return std::string_view("N/A");
		}

		bool kitSampleExists(size_t kitIdx, size_t sampleIdx) const {
			size_t bankIdx = KIT_LOOKUP[kitIdx];
			const char* kitData = (const char*)getBankData(bankIdx);

			size_t nameOffset = Kit::SAMPLE_NAME_OFFSET + sampleIdx * 3;
			return kitData[nameOffset] != 0;
		}

		Uint8Buffer getKitSampleData(size_t kitIdx, size_t sampleIdx) const {
			size_t bankIdx = KIT_LOOKUP[kitIdx];
			const uint8* kitData = getBankData(bankIdx);
			size_t nameOffset = Kit::SAMPLE_NAME_OFFSET + sampleIdx * 3;

			if (kitData[nameOffset] != 0) {
				size_t offset = sampleIdx * 2;
				size_t start = (0xFF & kitData[offset]) | ((0xFF & kitData[offset + 1]) << 8);
				size_t stop = (0xFF & kitData[offset + 2]) | ((0xFF & kitData[offset + 3]) << 8);

				if (stop > start) {
					return Uint8Buffer((uint8*)kitData + (start - BANK_SIZE), stop - start, false);
				}
			}

			return Uint8Buffer();
		}

		std::string_view getFontName(size_t idx) const {
			return std::string_view((const char*)_fontNames + idx * (Font::NAME_SIZE - 1), Font::NAME_SIZE);
		}

		std::string_view getPaletteName(size_t idx) const {
			return std::string_view((const char*)_paletteNames + idx * (Palette::NAME_SIZE - 1), Palette::NAME_SIZE);
		}

		void getFont(size_t idx, Font& font) const {
			const uint8* data = _fontData + idx * Font::SIZE + Font::HEADER_SIZE;

			//strncpy_s(font.name, (const char*)_fontNames + idx * (Font::NAME_SIZE - 1), Font::NAME_SIZE);

			for (size_t i = 0; i < Font::TILE_COUNT; ++i) {
				const uint8* tileData = data + i * Font::TILE_SIZE;

				for (uint32 y = 0; y < Font::TILE_HEIGHT; ++y) {
					for (uint32 x = 0; x < Font::TILE_WIDTH; ++x) {
						font.tiles[i].pixels[y * Font::TILE_WIDTH + x] = getTilePixel(tileData, x, y);
					}
				}
			}
		}

		Font getFont(size_t idx) const {
			Font ret;
			getFont(idx, ret);
			return ret;
		}

		Palette getPalette(size_t idx) const {
			const uint8* data = _paletteData + idx * Palette::SIZE;

			Palette ret;
			//strcpy_s(ret.name, (const char*)_paletteNames + idx * (Palette::NAME_SIZE - 1));

			for (size_t i = 0; i < Palette::COLOR_SET_COUNT; ++i) {
				const uint8* colorSet = data + i * Palette::COLOR_SET_SIZE;
				ret.sets[i].first = unpackColor(colorSet);
				ret.sets[i].second = unpackColor(colorSet + 3 * 2);
			}

			return ret;
		}

		Kit getNextEmptyKit() {
			int32 idx = nextEmptyKitIdx();
			if (idx != -1) {
				return Kit(getBankAccessor(KIT_LOOKUP[idx]), idx);
			}

			return Kit();
		}

		int32 nextEmptyKitIdx(size_t startIdx = 0) const {
			for (size_t i = startIdx; i < KIT_COUNT; ++i) {
				const uint8* bankData = getBankData(KIT_LOOKUP[i]);
				if (bankIsEmptyKit(bankData)) {
					return (int32)i;
				}
			}

			return -1;
		}

	private:
		bool bankIsKit(const Uint8Buffer& bankData) const {
			return bankData[0] == 0x60 && bankData[1] == 0x40;
		}

		bool bankIsEmptyKit(const Uint8Buffer& bankData) const {
			return bankData[0] == 0xFF && bankData[1] == 0xFF;
		}

		bool bankIsEmptyKit(const uint8* bankData) const {
			return bankData[0] == 0xFF && bankData[1] == 0xFF;
		}

		const uint32 BANK_VERSION_OFFSET = 0x5F;

		uint8 getBankVerion(const uint8* bankData) {
			return bankData[BANK_VERSION_OFFSET];
		}
	};
}
