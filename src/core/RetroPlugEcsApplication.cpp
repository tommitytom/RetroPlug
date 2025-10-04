#include "RetroPlugEcsApplication.h"

#include "RetroPlugEcsProcessor.h"
#include "RetroPlugEcsView.h"
#include "foundation/FsUtil.h"
#include "ecs/HexEditor.h"

namespace rp {
	RetroPlugEcsApplication::RetroPlugEcsApplication(): _audioEventNode("Audio"), _project(_audioEventNode->spawn("Ui"), _audioEventNode->getId()) {
		fw::FsUtil::setupFs();
	}

	fw::ViewPtr RetroPlugEcsApplication::onCreateUi() {
		return std::make_shared<RetroPlugEcsView>(getProject());
	}

	fw::ViewPtr RetroPlugEcsApplication::onCreateNamedView(const std::string& name) {
		if (name == "HexEditor") {
			return std::make_shared<HexEditor>();
		}
		return nullptr;
	}

	fw::AudioProcessorPtr RetroPlugEcsApplication::onCreateAudio() {
		return std::make_shared<RetroPlugEcsProcessor>(std::move(_audioEventNode.value()));
	}

	void RetroPlugEcsApplication::onUpdate(f32 deltaTime) {
		_project.onUpdate(deltaTime);
	}
}
