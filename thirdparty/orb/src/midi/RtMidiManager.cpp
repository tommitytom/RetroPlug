#include "midi/RtMidiManager.h"

#include "rtmidi/RtMidi.h"

namespace orb::midi {
	RtMidiManager::RtMidiManager() {
		_input = std::make_unique<rt::midi::RtMidiIn>();
		_output = std::make_unique<rt::midi::RtMidiOut>();                    
	}

	RtMidiManager::~RtMidiManager() {
	}

	void RtMidiManager::getInputDeviceNames(std::vector<std::string>& names) {
		unsigned int count = _input->getPortCount();
		for (unsigned int i = 0; i < count; ++i) {
			names.push_back(_input->getPortName(i));
		}
	}

	void RtMidiManager::getOutputDeviceNames(std::vector<std::string>& names) {
		unsigned int count = _output->getPortCount();
		for (unsigned int i = 0; i < count; ++i) {
			names.push_back(_output->getPortName(i));
		}
	}

	bool RtMidiManager::openInputDevice(int32 idx) {
		try {
			_input->openPort(idx);
			return true;
		} catch (const rt::midi::RtMidiError& e) {
			return false;
		}
	}

	bool RtMidiManager::openOutputDevice(int32 idx) {
		try {
			_output->openPort(idx);
			return true;
		} catch (const rt::midi::RtMidiError& e) {
			return false;
		}
	}

	void RtMidiManager::closeInputDevice() {
		_input->closePort();
	}

	void RtMidiManager::closeOutputDevice() {
		_output->closePort();
	}

	bool RtMidiManager::sendMessage(const std::vector<unsigned char>& message) {
		try {
			if (!_output->isPortOpen()) {
				return false;
			}
			_output->sendMessage(&message);
			return true;
		} catch (const rt::midi::RtMidiError& e) {
			return false;
		}
	}

	bool RtMidiManager::isInputDeviceOpen() const {
		return _input->isPortOpen();
	}

	bool RtMidiManager::isOutputDeviceOpen() const {
		return _output->isPortOpen();
	}

	bool RtMidiManager::getMessages(std::vector<MidiMessage>& messages) {
		std::vector<unsigned char> message;
		
		try {
			f64 stamp = _input->getMessage(&message);
			while (!message.empty()) {
				messages.push_back(MidiMessage{
					.status = message[0],
					.data1 = static_cast<uint8>(message.size() > 1 ? message[1] : 0),
					.data2 = static_cast<uint8>(message.size() > 2 ? message[2] : 0),
					.offset = 0
					//.offset = static_cast<uint32>(stamp * 1000.0)
				});

				message.clear();
				stamp = _input->getMessage(&message);
			}
		} catch (const rt::midi::RtMidiError& e) {
			return false;
		}
		
		return true;
	}
}
