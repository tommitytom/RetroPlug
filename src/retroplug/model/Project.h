#pragma once

#include "Types.h"
#include "util/DataBuffer.h"
#include <string>
#include <vector>
#include "Constants.h"
#include "model/ButtonStream.h"

const std::string PROJECT_VERSION = "1.0.0";

enum class GameboyModel {
	Auto,
	DmgB,
	//SgbNtsc,
	//SgbPal,
	//Sgb2,
	CgbC,
	CgbE,
	Agb
};

struct SameBoySettings {
	GameboyModel model = GameboyModel::Auto;
	bool gameLink = false;
	bool skipBootRom = false;
};

namespace math {
struct Point {
	float x, y;
	Point() : x(0), y(0) {}
	Point(float _x, float _y) : x(_x), y(_y) {}
};

struct Rect {
	float x, y, w, h;
	Rect(): x(0), y(0), w(0), h(0) {}
	Rect(float _x, float _y, float _w, float _h) : x(_x), y(_y), w(_w), h(_h) {}

	float bottom() const { return y + h; }

	float right() const { return x + w; }

	bool contains(const Point& point) const {
		return point.x >= x && point.x < right() && point.y >= y && point.y < bottom();
	}
};
}

struct SystemDesc {
	SystemIndex idx = NO_ACTIVE_SYSTEM;
	SystemType systemType = SystemType::Unknown;
	SystemState state = SystemState::Uninitialized;

	std::string romName;
	std::string romPath;
	std::string sramPath;

	math::Rect area;

	SameBoySettings sameBoySettings;

	DataBufferPtr romData;
	DataBufferPtr stateData;
	DataBufferPtr sramData;

	std::string keyInputConfig;
	std::string padInputConfig;
	std::string audioComponentState;
	std::string uiComponentState;

	GameboyButtonStream buttons;

	bool fastBoot = false;

	SystemDesc() {}
	SystemDesc(const SystemDesc& other) { *this = other; }

	void clear() { *this = SystemDesc(); }
};

using SystemDescPtr = std::shared_ptr<SystemDesc>;

struct Project {
	struct Settings {
		AudioChannelRouting audioRouting = AudioChannelRouting::StereoMixDown;
		MidiChannelRouting midiRouting = MidiChannelRouting::SendToAll;
		SystemLayout layout = SystemLayout::Auto;
		SaveStateType saveType = SaveStateType::Sram;
		int zoom = 2;
		bool includeRom = true;
	} settings;

	std::string path;
	std::vector<SystemDescPtr> systems;
	SystemIndex selectedSystem = NO_ACTIVE_SYSTEM;
};
