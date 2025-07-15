#include "System.h"

#include "core/SystemService.h"

namespace rp {
	void System::processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) {
		for (const SystemServicePtr& service : _services) {
			service->processInput(*this, buttons, actions);
		}

		if (_stream) {
			for (const fw::StreamButtonPress& stream : buttons) {
				_stream->input.buttons.push_back(stream);
			}
		}

		buttons.clear();
		actions.clear();
	}
}
