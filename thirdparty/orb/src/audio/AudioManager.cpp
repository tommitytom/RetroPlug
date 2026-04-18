#include "audio/AudioManager.h"

namespace orb::audio {
	AudioManager::AudioManager() {}
	AudioManager::~AudioManager() {}

	void AudioManager::setMidiManager(std::shared_ptr<midi::MidiManager> midiManager) {
		_midiManager = midiManager;

		std::vector<std::string> inputNames;
		std::vector<std::string> outputNames;
		midiManager->getInputDeviceNames(inputNames);
		midiManager->getOutputDeviceNames(outputNames);

		std::cout << "Detected " << inputNames.size() << " MIDI input devices and " << outputNames.size() << " MIDI output devices" << std::endl;

		for (size_t i = 0; i < inputNames.size(); ++i) {
			std::cout << "MIDI Input Device " << i << ": " << inputNames[i] << std::endl;
		}

		midiManager->openInputDevice(0);

		for (size_t i = 0; i < outputNames.size(); ++i) {
			std::cout << "MIDI Output Device " << i << ": " << outputNames[i] << std::endl;
		}
	}

	void AudioManager::process(f32* output, const f32* input, uint32 frameCount) {
		_processor->onBeginUpdate(frameCount);

		if (_midiManager) {
			std::vector<midi::MidiMessage> messages;
			if (_midiManager->getMessages(messages)) {
				for (const auto& message : messages) {
					_processor->onMidi(message);
				}
			}
		}

		_processor->onRender(output, input, frameCount);
	}
}
