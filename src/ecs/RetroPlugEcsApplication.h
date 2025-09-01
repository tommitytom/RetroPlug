#pragma once

#include "application/Application.h"
#include "RetroPlugProject.h"

namespace rp {
	class RetroPlugEcsApplication : public fw::app::Application {
	private:
		std::weak_ptr<RetroPlugProject> _project;
		fw::EventNode::NodeId _audioNodeId = 0;
		std::optional<fw::EventNode> _uiEventNode = std::nullopt;

	public:
		RetroPlugEcsApplication() = default;
		~RetroPlugEcsApplication() = default;

		fw::ViewPtr onCreateUi() override;

		fw::AudioProcessorPtr onCreateAudio() override;

		void onUpdate(f32 deltaTime) override;

		RetroPlugProjectPtr getProject();
	};
}
