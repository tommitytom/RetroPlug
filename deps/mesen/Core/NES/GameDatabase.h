#pragma once
#include "pch.h"
#include <atomic>
#include <unordered_map>
#include "NES/RomData.h"
#include "Utilities/SimpleLock.h"

struct NesHeader;

class GameDatabase
{
private:
	static std::unordered_map<uint32_t, GameInfo> _gameDatabase;
	static bool _enabled;
	// RetroPlug: an atomic, stored with release once the map is built and loaded with acquire before the unlocked
	// check, so a thread that sees it set also sees the map: every iNES load reaches InitDatabase, and two NES cores
	// built on two threads at once (the engine's offline pool, beside the live engine) raced on it as a plain bool.
	static std::atomic<bool> _initialized;
	static SimpleLock _loadLock;

	template<typename T> static T ToInt(string value);

	static BusConflictType GetBusConflictType(string busConflictSetting);
	static GameSystem GetGameSystem(string system);
	static uint8_t GetSubMapper(GameInfo &info);

	static void InitDatabase();
	static void UpdateRomData(GameInfo &info, RomData &romData);
	static void LoadGameDb(vector<string> data);

public:
	static void LoadGameDb(std::istream & db);

	static void SetGameInfo(uint32_t romCrc, RomData &romData, bool updateRomData, bool forHeaderlessRom);
	static bool GetiNesHeader(uint32_t romCrc, NesHeader &nesHeader);
	static bool GetDbRomSize(uint32_t romCrc, uint32_t &prgSize, uint32_t &chrSize);
};