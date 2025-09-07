#include "RetroPlugEcsApplication.h"

#include "RetroPlugEcsProcessor.h"
#include "RetroPlugEcsView.h"
#include "foundation/FsUtil.h"

#include <string>
#include <rfl/json.hpp>

namespace rp {
	RetroPlugEcsApplication::RetroPlugEcsApplication(): _audioEventNode("Audio"), _project(_audioEventNode->spawn("Ui"), _audioEventNode->getId()) {
		fw::FsUtil::setupFs();
	}

	fw::ViewPtr RetroPlugEcsApplication::onCreateUi() {
		return std::make_shared<RetroPlugEcsView>(getProject());
	}

	fw::AudioProcessorPtr RetroPlugEcsApplication::onCreateAudio() {
		return std::make_shared<RetroPlugEcsProcessor>(std::move(_audioEventNode.value()));
	}

	void RetroPlugEcsApplication::onUpdate(f32 deltaTime) {
		_project.onUpdate(deltaTime);
	}
}
