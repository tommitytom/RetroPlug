#include "RetroPlugApplication.h"

#include "core/RetroPlugProcessor.h"
#include "foundation/FsUtil.h"
#include "ui/RetroPlugView.h"
#include "ui/HexEditor.h"

namespace rp {
	RetroPlugApplication::RetroPlugApplication(): _audioEventNode("Audio"), _project(_audioEventNode->spawn("Ui"), _audioEventNode->getId()) {
		fw::FsUtil::setupFs();
	}

	fw::ViewPtr RetroPlugApplication::onCreateUi() {
		return std::make_shared<RetroPlugView>(getProject());
	}

	fw::ViewPtr RetroPlugApplication::onCreateNamedView(const std::string& name) {
		if (name == "HexEditor") {
			return std::make_shared<HexEditor>();
		}
		return nullptr;
	}

	fw::AudioProcessorPtr RetroPlugApplication::onCreateAudio() {
		return std::make_shared<RetroPlugProcessor>(std::move(_audioEventNode.value()));
	}

	void RetroPlugApplication::onUpdate(f32 deltaTime) {
		_project.onUpdate(deltaTime);
	}
}
