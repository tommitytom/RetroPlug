#pragma once

#include <string>
#include <vector>

#include "foundation/Types.h"
#include "midi/MidiMessage.h"

namespace orb::midi {
    class MidiManager {
    public:
        virtual ~MidiManager() = 0;
        virtual void getInputDeviceNames(std::vector<std::string>& names) = 0;
        virtual void getOutputDeviceNames(std::vector<std::string>& names) = 0;
        virtual bool openInputDevice(int32 idx) = 0;
        virtual bool openOutputDevice(int32 idx) = 0;
        virtual void closeInputDevice() = 0;
        virtual void closeOutputDevice() = 0;
        virtual bool sendMessage(const std::vector<unsigned char>& message) = 0;
        virtual bool isInputDeviceOpen() const = 0;
        virtual bool isOutputDeviceOpen() const = 0;
        virtual bool getMessages(std::vector<MidiMessage>& messages) = 0;
    };
}
