#pragma once

#include "libretroplug/MessageBus.h"
#include "roms/Lsdj.h"
#include "util/xstring.h"
#include <mutex>
#include <atomic>
#include <vector>

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

enum class SaveStateType {
	State,
	Sram
};

class SameBoyPlug;
using SameBoyPlugPtr = std::shared_ptr<SameBoyPlug>;

class SameBoyPlug {
private:
	void* _instance = nullptr;

	tstring _romPath;
	tstring _savePath;
	std::string _romName;

	MessageBus _bus;

	std::mutex _lock;
	std::atomic<bool> _midiSync = false;
	std::atomic<bool> _gameLink = false;
	std::atomic<int> _resetSamples = 0;

	Lsdj _lsdj;
	GameboyModel _model = GameboyModel::Auto;

	double _sampleRate = 48000;

	std::vector<std::byte> _romData;
	std::vector<std::byte> _saveData;
	SaveStateType _saveType = SaveStateType::State;

	bool _watchRom = false;

public:
	SameBoyPlug();
	~SameBoyPlug() { shutdown(); }

	bool watchRom() const { return false; }

	void setWatchRom(bool watch) { _watchRom = watch; }

	Lsdj& lsdj() { return _lsdj; }

	std::vector<std::byte>& romData() { return _romData; }

	void setRomPath(const tstring& path) { _romPath = path; }

	void setModel(GameboyModel model) { _model = model; }

	GameboyModel model() const { return _model; }

	bool midiSync() { return _midiSync.load(); }

	void setMidiSync(bool enabled) { _midiSync = enabled; }

	bool gameLink() const { return _gameLink.load(); }

	void setGameLink(bool enabled) { _gameLink = enabled; }

	void init(const tstring& romPath, GameboyModel model, bool fastBoot);

	void reset(GameboyModel model, bool fast);

	bool active() const { return _instance != nullptr; }

	const std::string& romName() const { return _romName; }

	const tstring& romPath() const { return _romPath; }

	std::mutex& lock() { return _lock; }

	void setSampleRate(double sampleRate);
	 
	void sendKeyboardByte(int offset, char byte);

	void sendSerialByte(int offset, char byte, size_t bitCount = 8);

	void sendMidiBytes(int offset, const char* bytes, size_t count);

	size_t saveStateSize();

	size_t batterySize();

	bool saveBattery(tstring path);

	bool saveBattery(std::vector<std::byte>& data);

	bool saveBattery(std::byte* data, size_t size);

	bool loadBattery(const tstring& path, bool reset);

	bool loadBattery(const std::vector<std::byte>& data, bool reset);

	bool loadBattery(const std::byte* data, size_t size, bool reset);

	bool clearBattery(bool reset);

	void saveState(std::vector<std::byte>& data);

	void saveState(std::byte* target, size_t size);

	void loadState(const std::vector<std::byte>& data);

	void loadState(const std::byte* source, size_t size);

	void setSetting(const std::string& name, int value);

	void setLinkTargets(std::vector<SameBoyPlugPtr> linkTargets);

	void setButtonState(const ButtonEvent& ev) { _bus.buttons.writeValue(ev); }

	void setButtonStateT(size_t buttonId, bool down) { _bus.buttons.writeValue(ButtonEvent{ buttonId, down }); }

	MessageBus* messageBus() { return &_bus; }

	void update(size_t audioFrames);

	void updateMultiple(SameBoyPlug** plugs, size_t plugCount, size_t audioFrames);

	void shutdown();

	void* instance() { return _instance; }

	void disableRendering(bool disable);

	void setSavePath(const tstring& path) { _savePath = path; }

	const tstring& savePath() const { return _savePath; }

	void updateRom();

private:
	void updateButtons();

	void updateAV(int audioFrames);
};
