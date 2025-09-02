#include "RetroPlugEcsApplication.h"

#include "RetroPlugEcsProcessor.h"
#include "RetroPlugEcsView.h"

namespace rp {
	RetroPlugEcsApplication::RetroPlugEcsApplication() {
		_audioEventNode = fw::EventNode("Audio");
		_project = std::make_shared<RetroPlugProject>(_audioEventNode->spawn("Ui"), _audioEventNode->getId());
	}

	fw::ViewPtr RetroPlugEcsApplication::onCreateUi() {
		return std::make_shared<RetroPlugEcsView>(getProject());
	}

	fw::AudioProcessorPtr RetroPlugEcsApplication::onCreateAudio() {
		auto view = std::make_shared<RetroPlugEcsProcessor>(std::move(_audioEventNode.value()));
		view->setSerializeHook([this](fw::Uint8Buffer& buffer) { _project->serialize(buffer); });
		view->setDeserializeHook([this](const fw::Uint8Buffer& buffer) { _project->deserialize(buffer); });
		return view;
	}

	void RetroPlugEcsApplication::onUpdate(f32 deltaTime) {
		// NOTE: Delta time is currently always 0
		_project->onUpdate(deltaTime);
	}

	RetroPlugProjectPtr RetroPlugEcsApplication::getProject() {
		return _project;
	}
}
