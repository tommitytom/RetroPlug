#include "audio/AudioController.h"

extern "C" {

int retroplug_init(double sampleRate) {
	AudioController controller(nullptr, sampleRate);
	return controller.getLuaContext() == nullptr ? 1 : 0;
}

}
