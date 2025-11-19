#pragma once

#include <algorithm>
//#include <spdlog/spdlog.h>
#include <liblsdj/liblsdj/include/lsdj/sav.h>
#include <liblsdj/liblsdj/include/lsdj/chain.h>
#include <liblsdj/liblsdj/include/lsdj/phrase.h>
#include <liblsdj/liblsdj/include/lsdj/instrument.h>
#include <liblsdj/liblsdj/include/lsdj/wave.h>

#include "foundation/DataBuffer.h"

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

		size_t getLength() const {
			return LSDJ_PHRASE_LENGTH;
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

		uint8 getPhraseCount() const {
			return LSDJ_CHAIN_LENGTH;
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
		Song(orb::Uint8Buffer& data) : _song((lsdj_song_t*)data.data()) {}

		bool isValid() const {
			return _song != nullptr;
		}

		lsdj_song_t* getRaw() {
			return _song;
		}

		const lsdj_song_t* getRaw() const {
			return _song;
		}

		orb::Uint8Buffer getBuffer() {
			return orb::Uint8Buffer((uint8*)_song, LSDJ_SONG_BYTE_COUNT);
		}

		orb::Uint8Buffer getSynthData(uint8 synth) const {
			return orb::Uint8Buffer(lsdj_wave_get_bytes(_song, synth * (LSDJ_WAVE_PER_SYNTH_COUNT + 1)), (LSDJ_WAVE_PER_SYNTH_COUNT + 1) * LSDJ_WAVE_BYTE_COUNT);
		}

		void setSynthData(uint8 synth, const orb::Uint8Buffer& buffer) {
			assert(buffer.size() == 256);
			lsdj_wave_set_bytes(_song, synth * (LSDJ_WAVE_PER_SYNTH_COUNT + 1), buffer.data());
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

		uint8 getChainCount() const {
			return LSDJ_CHAIN_COUNT;
		}

		uint8 getPhraseCount() const {
			return LSDJ_PHRASE_COUNT;
		}

		uint8 getInstrumentCount() const {
			return LSDJ_INSTRUMENT_COUNT;
		}

		lsdj::Phrase getPhrase(uint8 index) {
			return lsdj::Phrase(_song, index);
		}

		lsdj::Instrument getInstrument(uint8 index) {
			return lsdj::Instrument(_song, index);
		}

		bool isRowBookMarked(uint8 channel, uint8 row) const {
			return lsdj_song_is_row_bookmarked(_song, row, (lsdj_channel_t)channel);
		}
	};

	class Project {
	private:
		lsdj_project_t* _project = nullptr;
		uint8 _projectIndex = LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX;
		bool _ownsData = false;

	public:
		static Project fromLsdsng(const orb::Uint8Buffer& buffer) {
			lsdj_project_t* project = nullptr;
			lsdj_error_t err = lsdj_project_read_lsdsng_from_memory(buffer.data(), buffer.size(), &project, nullptr);
			if (err != LSDJ_SUCCESS) {
				//spdlog::warn("Failed to read .lsdsng data");
				return Project();
			}

			return Project(project, true, LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX);
		}

		Project() {}
		Project(lsdj_project_t* project, bool ownsData, uint8 projectIndex) : _project(project), _projectIndex(projectIndex), _ownsData(ownsData) {}
		Project(orb::Uint8Buffer& buffer) : _project((lsdj_project_t*)buffer.data()), _ownsData(false) {}
		~Project() {
			if (_ownsData && _project) {
				lsdj_project_free(_project);
				_project = nullptr;
			}
		}

		Project& operator=(const Project& other) {
			if (_ownsData && _project) {
				lsdj_project_free(_project);
				_project = nullptr;
				_ownsData = false;
			}

			if (other._ownsData) {
				lsdj_error_t err = lsdj_project_copy(other._project, &_project, nullptr);
				_ownsData = (err == LSDJ_SUCCESS);
			} else {
				_project = other._project;
				_ownsData = false;
			}

			_projectIndex = other._projectIndex;

			return *this;
		}

		Project(const Project& other) { *this = other; }

		Project& operator=(Project&& other) noexcept {
			if (_ownsData && _project) {
				lsdj_project_free(_project);
			}
			_project = other._project;
			_projectIndex = other._projectIndex;
			_ownsData = other._ownsData;
			other._project = nullptr;
			other._projectIndex = LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX;
			other._ownsData = false;
			return *this;
		}
		Project(Project&& other) noexcept { *this = std::move(other); };

		uint8 getIndex() const {
			return _projectIndex;
		}

		uint8 getVersion() const {
			return lsdj_project_get_version(_project);
		}

		std::string_view getName() const {
			return std::string_view(lsdj_project_get_name(_project), lsdj_project_get_name_length(_project));
		}

		void setName(const std::string& name) {
			lsdj_project_set_name(_project, name.c_str());
		}

		Song getSong() const {
			return lsdj_project_get_song(_project);
		}

		bool isValid() const {
			return _project != nullptr;
		}

		lsdj_project_t* getRaw() {
			return _project;
		}

		const lsdj_project_t* getRaw() const {
			return _project;
		}

		bool toLsdsng(orb::Uint8Buffer& target) const {
			target.resize(LSDSNG_MAX_SIZE);
			size_t writeCount = 0;
			lsdj_error_t err = lsdj_project_write_lsdsng_to_memory(_project, target.data(), &writeCount);
			if (err == LSDJ_SUCCESS) {
				target.resize(writeCount);
				return true;
			}

			return false;
		}
	};

	class Sav {
	private:
		lsdj_sav_t* _sav = nullptr;

	public:
		Sav() {
			lsdj_sav_new(&_sav, nullptr);
		}

		Sav(const orb::Uint8Buffer& data) {
			load(data);
		}

		// Copy constructor
		Sav(const Sav& other) {
			if (other._sav) {
				lsdj_error_t err = lsdj_sav_copy(other._sav, &_sav, nullptr);
				if (err != LSDJ_SUCCESS) {
					_sav = nullptr;
				}
			} else {
				lsdj_sav_new(&_sav, nullptr);
			}
		}

		// Copy assignment operator
		Sav& operator=(const Sav& other) {
			if (this != &other) {
				free();
				if (other._sav) {
					lsdj_error_t err = lsdj_sav_copy(other._sav, &_sav, nullptr);
					if (err != LSDJ_SUCCESS) {
						_sav = nullptr;
					}
				} else {
					lsdj_sav_new(&_sav, nullptr);
				}
			}
			return *this;
		}

		// Move constructor
		Sav(Sav&& other) noexcept : _sav(other._sav) {
			other._sav = nullptr;
		}

		// Move assignment operator
		Sav& operator=(Sav&& other) noexcept {
			if (this != &other) {
				free();
				_sav = other._sav;
				other._sav = nullptr;
			}
			return *this;
		}

		~Sav() {
			free();
		}

		void free() {
			if (_sav) {
				lsdj_sav_free(_sav);
				_sav = nullptr;
			}
		}

		bool isValid() const {
			return _sav != nullptr;
		}

		lsdj_error_t load(const uint8* data, size_t size) {
			free();
			return lsdj_sav_read_from_memory(data, size, &_sav, nullptr);
		}

		lsdj_error_t load(const orb::Uint8Buffer& data) {
			return load(data.data(), data.size());
		}

		bool save(orb::Uint8Buffer& target) {
			target.resize(LSDJ_SAV_SIZE);
			size_t writeCount;
			lsdj_error_t err = lsdj_sav_write_to_memory(_sav, target.data(), target.size(), &writeCount);

			 if (err != LSDJ_SUCCESS) {
				 //spdlog::error("Failed to set project at index {}: error {}", idx, lsdj_error_get_description(error));
				 return false;
			 }

			 return true;
		}

		orb::Uint8Buffer save() {
			orb::Uint8Buffer data;
			save(data);
			return data;
		}

		uint32 getTotalProjectCount() const {
			return LSDJ_SAV_PROJECT_COUNT;
		}

		// TODO: Rename this to getActiveProjectCount. Iterating over this and accessing by index would be bad!
		uint32 getProjectCount() const {
			uint32 count = 0;
			for (uint8 i = 0; i < LSDJ_SAV_PROJECT_COUNT; ++i) {
				lsdj_project_t* proj = lsdj_sav_get_project(_sav, i);

				if (proj) {
					count++;
				}

			}

			return count;
		}

		void eraseProject(uint8 idx) {
			lsdj_sav_erase_project(_sav, idx);
		}

		uint8 findNextEmptyProject() {
			for (uint8 i = 0; i < LSDJ_SAV_PROJECT_COUNT; ++i) {
				if (!lsdj_sav_get_project_const(_sav, i)) {
					return i;
				}
			}
			return 255;
		}

		Project getProject(uint8 idx) const {
			return Project(lsdj_sav_get_project(_sav, idx),	false, idx);
		}

		bool setProject(const Project& project, uint8 idx) {
			lsdj_error_t error = lsdj_sav_set_project_copy(_sav, idx, project.getRaw(), nullptr);
			if (error != LSDJ_SUCCESS) {
				//spdlog::error("Failed to set project at index {}: error {}", idx, lsdj_error_get_description(error));
				return false;
			}

			return true;
		}

		bool addProject(const Project& project) {
			uint8 idx = findNextEmptyProject();

			if (idx == 255) {
				return false;
			}

			return setProject(project, idx);
		}

		Project getWorkingProject() const {
			uint8 idx = lsdj_sav_get_active_project_index(_sav);
			if (idx != LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX) {
				return getProject(idx);
			}

			return Project();
		}

		Song getWorkingSong() const {
			return lsdj_sav_get_working_memory_song(_sav);
		}

		void setWorkingSong(const Song& song) {
			lsdj_sav_set_working_memory_song(_sav, song.getRaw());
		}

		void setWorkingProject(uint8 idx) {
			lsdj_error_t err = lsdj_sav_set_working_memory_song_from_project(_sav, idx);
			if (err != LSDJ_SUCCESS) {
				//spdlog::error("Failed to set working project to index {}: error {}", idx, lsdj_error_get_description(err));
			}
		}
	};
}
