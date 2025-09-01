#include "RetroPlugEcsApplication.h"

#include "RetroPlugEcsProcessor.h"
#include "RetroPlugEcsView.h"

namespace rp {
	fw::ViewPtr RetroPlugEcsApplication::onCreateUi() {
		return std::make_shared<RetroPlugEcsView>(getProject());
	}

	fw::AudioProcessorPtr RetroPlugEcsApplication::onCreateAudio() {
		auto processor = std::make_shared<RetroPlugEcsProcessor>();
		fw::EventNode& eventNode = processor->getEventNode();
		_audioNodeId = eventNode.getId();
		_uiEventNode = eventNode.spawn("Ui");
		return processor;
	}

	void RetroPlugEcsApplication::onUpdate(f32 deltaTime) {
		// NOTE: Delta time is currently always 0
		RetroPlugProjectPtr project = _project.lock();
		if (project) {
			project->onUpdate(deltaTime);
		}
	}

	RetroPlugProjectPtr RetroPlugEcsApplication::getProject() {
		RetroPlugProjectPtr project = _project.lock();
		if (!project) {
			assert(_uiEventNode.has_value());
			project = std::make_shared<RetroPlugProject>(std::move(_uiEventNode.value()), _audioNodeId);
			_project = project;
		}

		return project;
	}
}
