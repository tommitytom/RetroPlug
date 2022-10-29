#pragma once

#include <string>

#include "audio/AudioManager.h"
#include "core/AudioContext.h"
#include "foundation/DataBuffer.h"
#include "ui/View.h"

namespace rp {
	enum class ThreadTarget {
		Ui,
		Audio
	};

	const size_t MAX_IO_STREAMS = 16;
	class Project;
	class FileManager;

	class RetroPlug final : public fw::View {
	private:
		f64 _nextFrame = 0;

		fw::Uint8Buffer _romBuffer;
		fw::Uint8Buffer _savBuffer;

		std::string _romPath;
		std::string _savPath;

		bool _ready = false;

		IoMessageBus _ioMessageBus;
		OrchestratorMessageBus _orchestratorMessageBus;

		std::shared_ptr<AudioContext> _audioContext;

		UiState _state;
		Project* _project;
		FileManager* _fileManager;

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

		bool onKey(const fw::KeyEvent& ev) override;

	private:
		void processInput(uint32 frameCount);

		void processOutput();
	};
}
