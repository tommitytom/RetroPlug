#pragma once

//#include <lsdj/sav.h>
#include <algorithm>
#include <liblsdj/liblsdj/include/lsdj/sav.h>
#include <liblsdj/liblsdj/include/lsdj/chain.h>
#include <liblsdj/liblsdj/include/lsdj/phrase.h>
#include <liblsdj/liblsdj/include/lsdj/instrument.h>
#include <liblsdj/liblsdj/include/lsdj/wave.h>

#include "util/DataBuffer.h"

namespace rp::lsdj {
	class Instrument {
	private:
		lsdj_song_t* _song = nullptr;
		uint8 _instrumentIdx = LSDJ_PHRASE_NO_INSTRUMENT;

	public:
		Instrument() {}
		Instrument(lsdj_song_t* song, uint8 instrumentIdx) : _song(song), _instrumentIdx(instrumentIdx) {}

		lsdj_instrument_type_t getType() const {
			return lsdj_instrument_get_type(_song, _instrumentIdx);
		}

		uint8 getKit1() const {
			return lsdj_instrument_kit_get_kit1(_song, _instrumentIdx);
		}

		uint8 getKit2() const {
			return lsdj_instrument_kit_get_kit2(_song, _instrumentIdx);
		}

		bool isValid() const {
			return _instrumentIdx != LSDJ_PHRASE_NO_INSTRUMENT && _instrumentIdx < LSDJ_INSTRUMENT_COUNT;
		}

		uint8 getIndex() const {
			return _instrumentIdx;
		}
	};

	class Phrase {
	private:
		lsdj_song_t* _song = nullptr;
		uint8 _phraseIdx = LSDJ_CHAIN_NO_PHRASE;

	public:
		Phrase() {}
		Phrase(lsdj_song_t* song, uint8 phraseIdx): _song(song), _phraseIdx(phraseIdx) {}

		uint8 getNote(uint8 step) const {
			return lsdj_phrase_get_note(_song, _phraseIdx, step);
		}

		uint8 getInstrumentIndex(uint8 step) const {
			return lsdj_phrase_get_instrument(_song, _phraseIdx, step);
		}

		Instrument getInstrument(uint8 step) const {
			uint8 idx = lsdj_phrase_get_instrument(_song, _phraseIdx, step);
			return Instrument(_song, idx);
		}

		lsdj_command_t getCommand(uint8 step) const {
			return lsdj_phrase_get_command(_song, _phraseIdx, step);
		}

		uint8 getCommandValue(uint8 step) const {
			return lsdj_phrase_get_command_value(_song, _phraseIdx, step);
		}

		uint8 getIndex() const {
			return _phraseIdx;
		}

		bool isValid() const {
			return _phraseIdx != LSDJ_CHAIN_NO_PHRASE;
		}
	};

	class Chain {
	private:
		lsdj_song_t* _song = nullptr;
		uint8 _chainIdx = LSDJ_SONG_NO_CHAIN;

	public:
		Chain() {}
		Chain(lsdj_song_t* song, uint8 chainIdx): _song(song), _chainIdx(chainIdx) {}

		uint8 getPhraseIndex(uint8 step) const {
			return lsdj_chain_get_phrase(_song, _chainIdx, step);
		}

		Phrase getPhrase(uint8 step) const {
			return Phrase(_song, getPhraseIndex(step));
		}

		uint8 getPhraseTransposition(uint8 step) const {
			return lsdj_chain_get_transposition(_song, _chainIdx, step);
		}

		uint8 getIndex() const {
			return _chainIdx;
		}

		bool isValid() const {
			return _chainIdx != LSDJ_SONG_NO_CHAIN;
		}
	};

	class Song {
	private:
		lsdj_song_t* _song = nullptr;

	public:
		Song() {}
		Song(lsdj_song_t* song): _song(song) {}
		Song(uint8* data): _song((lsdj_song_t*)data) {}

		Uint8Buffer getBuffer() {
			return Uint8Buffer((uint8*)_song, LSDJ_SONG_BYTE_COUNT);
		}

		Uint8Buffer getSynthData(uint8 synth) const {
			return Uint8Buffer(lsdj_wave_get_bytes(_song, synth * LSDJ_WAVE_PER_SYNTH_COUNT), LSDJ_WAVE_PER_SYNTH_COUNT * LSDJ_WAVE_BYTE_COUNT);
		}

		void setSynthData(uint8 synth, const Uint8Buffer& buffer) {
			size_t writeSize = std::min(buffer.size(), (size_t)(LSDJ_WAVE_PER_SYNTH_COUNT * LSDJ_WAVE_BYTE_COUNT));
			uint8* data = lsdj_wave_get_bytes(_song, synth * LSDJ_WAVE_PER_SYNTH_COUNT);
			memcpy(data, buffer.data(), writeSize);
		}

		uint8 getChainIndex(lsdj_channel_t channel, uint8 row) const {
			return lsdj_row_get_chain(_song, (uint8)row, channel);
		}

		uint8 getChainIndex(uint8 channel, uint8 row) const {
			return lsdj_row_get_chain(_song, (uint8)row, (lsdj_channel_t)channel);
		}

		Chain getChain(uint8 channel, uint8 row) const {
			return Chain(_song, getChainIndex(channel, row));
		}

		uint8 getFontIndex() const {
			return lsdj_song_get_font(_song);
		}

		uint8 getPaletteIndex() const {
			return lsdj_song_get_color_palette(_song);
		}

		bool isRowBookMarked(uint8 channel, uint8 row) const {
			return lsdj_song_is_row_bookmarked(_song, row, (lsdj_channel_t)channel);
		}
	};

	class Project {
	private:
		lsdj_project_t* _project = nullptr;

	public:
		Project(): _project(nullptr) {}
		Project(lsdj_project_t* project): _project(project) {}

		uint8 getVersion() const {
			return lsdj_project_get_version(_project);
		}

		std::string_view getName() const {
			return std::string_view(lsdj_project_get_name(_project), lsdj_project_get_name_length(_project));
		}

		Song getSong() const {
			return lsdj_project_get_song(_project);
		}

		bool isValid() const {
			return _project;
		}
	};

	class Sav {
	private:
		lsdj_sav_t* _sav = nullptr;

	public:
		Sav() {}
		Sav(const Uint8Buffer& data) {
			load(data);
		}

		bool isValid() const {
			return _sav != nullptr;
		}

		lsdj_error_t load(const uint8* data, size_t size) {
			if (_sav) {
				lsdj_sav_free(_sav);
				_sav = nullptr;
			}

			return lsdj_sav_read_from_memory(data, size, &_sav, nullptr);
		}

		lsdj_error_t load(const Uint8Buffer& data) {
			return load(data.data(), data.size());
		}

		Uint8Buffer save() {
			Uint8Buffer data(LSDJ_SAV_SIZE);
			size_t writeCount;
			lsdj_sav_write_to_memory(_sav, data.data(), data.size(), &writeCount);
			return data;
		}

		Project getProject(uint8 idx) const {
			return lsdj_sav_get_project(_sav, idx);
		}

		Song getWorkingSong() const {
			return lsdj_sav_get_working_memory_song(_sav);
		}

		void setWorkingProject(uint8 idx) {
			lsdj_error_t err = lsdj_sav_set_working_memory_song_from_project(_sav, idx);
			assert(err == LSDJ_SUCCESS);
		}
	};
}
