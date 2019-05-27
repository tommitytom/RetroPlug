#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "liblsdj/error.h"
#include "liblsdj/project.h"
#include "liblsdj/sav.h"

enum class LsdjSyncModes {
	Off,
	Slave,
	Master,
	Midi,
	Nanoloop,
	Keyboard,
	AnalogIn,
	AnalogOut,
	MidiMap,
	MidiOut,
	MidiArduinoboy
};

static int midiMapRowNumber(int channel, int noteNumber) {
	if (channel == 0) {
		return noteNumber;
	} else if (channel == 1) {
		return noteNumber + 128;
	}

	return -1;
}

static std::string syncModeToString(LsdjSyncModes syncMode) {
	switch (syncMode) {
	case LsdjSyncModes::Midi: return "midiSync";
	case LsdjSyncModes::MidiArduinoboy: return "midiSyncArduinoboy";
	case LsdjSyncModes::MidiMap: return "midiMap";
	}

	return "off";
}

static LsdjSyncModes syncModeFromString(const std::string& syncMode) {
	if (syncMode == "midiSync") return LsdjSyncModes::Midi;
	if (syncMode == "midiSyncArduinoboy") return LsdjSyncModes::MidiArduinoboy;
	if (syncMode == "midiMap") return LsdjSyncModes::MidiMap;
	return LsdjSyncModes::Off;
}

class Lsdj {
public:
	bool found = true;
	std::string version;
	bool arduinoboyPlaying = false;
	int tempoDivisor = 1;
	int lastRow = -1;

	std::atomic<LsdjSyncModes> syncMode = LsdjSyncModes::Off;
	std::atomic<bool> autoPlay = false;
	std::atomic<bool> keyboardShortcuts = true;

	std::vector<char> saveData;

	void removeSong(int idx) {

	}

	void exportSong(int idx, std::vector<char>& target) {
		if (saveData.size() == 0) {
			return;
		}

		lsdj_error_t* error = nullptr;
		lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
		if (sav == nullptr) {
			return;
		}

		target.resize(LSDJ_SONG_DECOMPRESSED_SIZE);

		lsdj_project_t* project = lsdj_sav_get_project(sav, idx);
		lsdj_project_write_lsdsng_to_memory(project, (unsigned char*)target.data(), target.size(), &error);

		lsdj_sav_free(sav);
	}

	void getSongNames(std::vector<std::string>& names) {
		if (saveData.size() == 0) {
			return;
		}

		lsdj_error_t* error = nullptr;
		lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
		if (sav == nullptr) {
			return;
		}

		char name[9];
		std::fill_n(name, 9, '\0');

		lsdj_project_t* current = lsdj_project_new_from_working_memory_song(sav, &error);
		lsdj_project_get_name(current, name, sizeof(name));
		names.push_back(std::string(name) + " (working)");

		size_t count = lsdj_sav_get_project_count(sav);
		for (size_t i = 0; i < count; ++i) {
			lsdj_project_t* project = lsdj_sav_get_project(sav, i);
			if (lsdj_project_get_song(project) != NULL) {
				std::fill_n(name, 9, '\0');
				lsdj_project_get_name(project, name, sizeof(name));
				names.push_back(std::string(name) + (project == current ? " (current)" : ""));
			}
		}

		lsdj_sav_free(sav);
	}
};
