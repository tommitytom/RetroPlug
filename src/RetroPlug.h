#pragma once

#include <string>

#include "platform/AudioManager.h"
#include "platform/Application.h"
#include "core/AudioContext.h"
#include "core/UiContext.h"
#include "util/DataBuffer.h"

struct NVGcontext;

namespace rp {
	class RetroPlug {
	private:
		NVGcontext* _context = nullptr;

		f64 _nextFrame = 0;

		Uint8Buffer _romBuffer;
		Uint8Buffer _savBuffer;

		std::string _romPath;
		std::string _savPath;

		bool _ready = false;

		IoMessageBus _ioMessageBus;
		OrchestratorMessageBus _orchestratorMessageBus;

		UiContext _uiContext;
		AudioContext _audioContext;

	public:
		RetroPlug();

		AudioContext& getAudioContext() {
			return _audioContext;
		}

		UiContext& getUiContext() {
			return _uiContext;
		}

		void setAudioManager(AudioManager& audioManager) {
			_uiContext.setAudioManager(audioManager);
		}

		void init() {

		}
	};
}
