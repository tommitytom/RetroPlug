#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "liblsdj/error.h"
#include "liblsdj/project.h"
#include "liblsdj/sav.h"

#include "platform/Logger.h"
#include "util/xstring.h"

enum class LsdjSyncModes {
	Off,
	Slave,
	Master,
	Midi,
	Nanoloop,
	KeyboardArduinoboy,
	AnalogIn,
	AnalogOut,
	MidiMap,
	MidiOut,
	MidiArduinoboy
};

enum class LsdjKeyboard : uint8_t {
	OctDn = 0x05,
	OctUp = 0x06,

	InsDn = 0x04,
	InsUp = 0x0C,

	TblDn = 0x03,
	TblUp = 0x0B,

	TblCue = 0x29,

	Mut1 = 0x01,
	Mut2 = 0x09,
	Mut3 = 0x78,
	Mut4 = 0x07,

	CurL = 0x6B,
	CurR = 0x74,
	CurU = 0x75,
	CurD = 0x72,
	PgUp = 0x7D,
	PgDn = 0x7A,
	Entr = 0x5A
};

const uint8_t LsdjKeyboardStartOctave = 36;
const uint8_t LsdjKeyboardNoteStart = 48;

const uint8_t LsdjKeyboardNoteMap[24] = {	0x1A,0x1B,0x22,0x23,0x21,0x2A,0x34,0x32,0x33,0x31,0x3B,0x3A,
											0x15,0x1E,0x1D,0x26,0x24,0x2D,0x2E,0x2C,0x36,0x35,0x3D,0x3C		};

const uint8_t LsdjKeyboardLowOctaveMap[12] = {
	0x01, //Mute1
	0x09, //Mute2
	0x78, //Mute3
	0x07, //Mute4
	0x68, //Cursor Left
	0x74, //Cursor Right
	0x75, //Cursor Up
	0x72, //Cursor Down
	0x5A, //Enter
	0x7A, //Table Up
	0x7D, //Table Down
	0x29  //Table Cue
};

const uint32_t LsdjKitHashes[22] = {	3734161118, 3816718742, 3788323431, 2132077591, 341848934, 4094864613, 
										1619355490, 1432911456, 2269540319, 3452550342, 2014800155, 3671301996, 
										2468775071, 1244551999, 1123275968, 2188962532, 2840583983, 1977389053,
										176697863, 3590495210, 2373983532, 3719557227	};

static bool isBuiltInKit(uint32_t hash) {
	for (int i = 0; i < 22; i++) {
		if (LsdjKitHashes[i] == hash) {
			return true;
		}
	}

	return false;
}

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
	case LsdjSyncModes::KeyboardArduinoboy: return "keyboardArduinoboy";
	}

	return "off";
}

static LsdjSyncModes syncModeFromString(const std::string& syncMode) {
	if (syncMode == "midiSync") return LsdjSyncModes::Midi;
	if (syncMode == "midiSyncArduinoboy") return LsdjSyncModes::MidiArduinoboy;
	if (syncMode == "midiMap") return LsdjSyncModes::MidiMap;
	if (syncMode == "keyboardArduinoboy") return LsdjSyncModes::KeyboardArduinoboy;
	return LsdjSyncModes::Off;
}

struct LsdjSongName {
	int projectId;
	std::string name;
	unsigned char version;
};

struct NamedData {
	std::string name;
	std::vector<std::byte> data;
};

struct NamedHashedData {
	std::string name;
	std::vector<std::byte> data;
	uint32_t hash;
};

using NamedDataPtr = std::shared_ptr<NamedData>;
using NamedHashedDataPtr = std::shared_ptr<NamedHashedData>;

class Lsdj {
public:
	Lsdj();

	void loadRom(const std::vector<std::byte>& romData);

	// Song specific

	std::vector<int> importSongs(const std::vector<tstring>& paths, std::string& error);

	void loadSong(int idx);

	void exportSong(int idx, std::vector<std::byte>& target);

	void exportSongs(std::vector<NamedData>& target);

	void deleteSong(int idx);

	void getSongNames(std::vector<LsdjSongName>& names);

	// Kit specific

	bool loadRomKits(const std::vector<std::byte>& romData, bool absolute, std::string& error);

	bool loadKit(const tstring& path, int idx, std::string& error);
	
	void getKitNames(std::vector<std::string>& names);

	void patchKit(std::vector<std::byte>& romData, const std::vector<std::byte>& kitData, int index);

	void exportKit(const std::vector<std::byte>& romData, int idx, std::vector<std::byte>& target);

	void deleteKit(std::vector<std::byte>& romData, int index);

	int findEmptyKit() {
		for (size_t i = 0; i < kitData.size(); ++i) {
			if (kitData[i] == nullptr) {
				return i;
			}
		}

		return -1;
	}

	void clearKits();

	void readKit(const std::vector<std::byte>& romData, std::vector<std::byte>& target, int index);

	void patchKits(std::vector<std::byte>& romData);

private:
	void loadKitAt(const char* data, size_t size, int idx);
};
