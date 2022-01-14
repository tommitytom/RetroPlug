#pragma once

#include "platform/Types.h"
#include "util/DataBuffer.h"
#include "core/MemoryAccessor.h"

namespace rp::lsdj {
	enum class ScreenType {
		Unknown,
		Song,
		Chain,
		Phrase,
		Instrument,
		Table,
		Project,
		Wave,
		Synth,
		Groove
	};

	struct MemoryOffsets {
		struct Channel {
			uint32 active = 0;
			uint32 songPosition = 0;
			uint32 chainPosition = 0;
			uint32 phrasePosition = 0;
		};

		Channel channels[4] = { 0, 0, 0, 0 };
		uint32 tempo = 0;
		uint32 cursorX = 0;
		uint32 cursorY = 0;
		uint32 screenX = 0;
		uint32 screenY = 0;

		bool operator==(const MemoryOffsets& other) const {
			for (size_t i = 0; i < 4; ++i) {
				if (channels[i].active != other.channels[i].active) return false;
				if (channels[i].songPosition != other.channels[i].songPosition) return false;
				if (channels[i].chainPosition != other.channels[i].chainPosition) return false;
				if (channels[i].phrasePosition != other.channels[i].phrasePosition) return false;
			}

			return true;
		}

		bool operator!=(const MemoryOffsets& other) const {
			return !(*this == other);
		}
	};

	class Ram {
	private:
		MemoryAccessor _data;
		MemoryOffsets _offsets;

	public:
		Ram() {}
		Ram(MemoryAccessor data, const MemoryOffsets& offsets): _data(data), _offsets(offsets) {}

		bool isValid() const {
			return _data.isValid();
		}

		void setData(MemoryAccessor data) {
			_data = data;
		}

		void setOffsets(const MemoryOffsets& offsets) {
			_offsets = offsets;
		}

		bool isChannelActive(uint8 channel) const {
			return _data[_offsets.channels[channel].active] > 0;
		}

		uint8 getSongPosition(uint8 channel) const {
			return _data[_offsets.channels[channel].songPosition];
		}

		uint8 getChainPosition(uint8 channel) const {
			return _data[_offsets.channels[channel].chainPosition];
		}

		uint8 getPhrasePosition(uint8 channel) const {
			return _data[_offsets.channels[channel].phrasePosition];
		}

		void setCursorPosition(Point<uint8> pos) {
			_data.set(_offsets.cursorX, pos.x);
			_data.set(_offsets.cursorY, pos.y);
		}

		Point<uint8> getCursorPosition() const {
			return { _data[_offsets.cursorX], _data[_offsets.cursorY] };
		}

		uint8 getCursorX() const {
			return _data[_offsets.cursorX];
		}

		void setCursorX(uint8 x) {
			_data.set(_offsets.cursorX, x);
		}

		uint8 getCursorY() const {
			return _data[_offsets.cursorY];
		}

		uint8 getScreenX() const {
			return _data[_offsets.screenX];
		}

		uint8 getScreenY() const {
			return _data[_offsets.screenY];
		}

		ScreenType getScreen() const {
			// TODO: This is dependent on version - find differences!

			uint8 x = getScreenX();
			uint8 y = getScreenY();

			switch (y) {
				case 255: {
					switch (x) {
					case 0:
					case 1: return ScreenType::Project;
					case 2: return ScreenType::Wave;
					case 3: return ScreenType::Synth;
					case 4: return ScreenType::Table;
					}

					break;
				}
				case 0: {
					switch (x) {
					case 0: return ScreenType::Song;
					case 1: return ScreenType::Chain;
					case 2: return ScreenType::Phrase;
					case 3: return ScreenType::Instrument;
					case 4: return ScreenType::Table;
					}

					break;
				}
				case 1: {
					return ScreenType::Groove;
				}
			}

			return ScreenType::Unknown;
		}

		uint8 getTempo() const {
			return 240;
		}
	};
}
