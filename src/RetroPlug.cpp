#include "RetroPlug.h"

#include "util/SolUtil.h"

using namespace rp;

RetroPlug::RetroPlug() : _uiContext(&_ioMessageBus, &_orchestratorMessageBus), _audioContext(&_ioMessageBus, &_orchestratorMessageBus) {
	
}
