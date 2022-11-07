#pragma once

#include <string>

#include "audio/AudioManager.h"
#include "core/AudioContext.h"
#include "foundation/DataBuffer.h"
#include "ui/View.h"

enum class ThreadTarget {
	Ui,
	Audio
};

const size_t MAX_IO_STREAMS = 16;

namespace rp {
	class Project;
	class FileManager;
}

class RetroPlug final : public fw::View {
private:
	f64 _nextFrame = 0;

	fw::Uint8Buffer _romBuffer;
	fw::Uint8Buffer _savBuffer;

	std::string _romPath;
	std::string _savPath;

	bool _ready = false;

	rp::IoMessageBus _ioMessageBus;
	rp::OrchestratorMessageBus _orchestratorMessageBus;

	std::shared_ptr<rp::AudioContext> _audioContext;

	rp::UiState _state;
	rp::Project* _project;
	rp::FileManager* _fileManager;

	//SystemIndex _selected = INVALID_SYSTEM_IDX;

	uint32 _sampleRate = 48000;

	ThreadTarget _defaultTarget = ThreadTarget::Audio;

	//std::vector<SystemIoPtr> _ioCollection;
	size_t _totalIoAllocated = 0;

public:
	RetroPlug();
	~RetroPlug() = default;

	void onInitialize() override;

	void onUpdate(f32 delta) override;

	void onRender(Canvas& canvas) override;

private:
	void processInput(uint32 frameCount);

	void processOutput();
};
