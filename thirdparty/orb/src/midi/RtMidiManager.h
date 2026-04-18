#pragma once

#include <memory>
#include <vector>

#include "midi/MidiManager.h"

namespace rt::midi {
    class RtMidiIn;
    class RtMidiOut;
}

namespace orb::midi {
    class RtMidiManager : public MidiManager {
    private:
        std::unique_ptr<rt::midi::RtMidiIn> _input;
        std::unique_ptr<rt::midi::RtMidiOut> _output;

    public:
        RtMidiManager();
        virtual ~RtMidiManager();
        void getInputDeviceNames(std::vector<std::string>& names);
        void getOutputDeviceNames(std::vector<std::string>& names);
        bool openInputDevice(int32 idx);
        bool openOutputDevice(int32 idx);
        void closeInputDevice();
        void closeOutputDevice();
        bool sendMessage(const std::vector<unsigned char>& message);
        bool isInputDeviceOpen() const;
        bool isOutputDeviceOpen() const;
        bool getMessages(std::vector<MidiMessage>& messages);
    };
}
