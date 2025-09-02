#pragma once

#include "application/Application.h"
#include "RetroPlugProject.h"

namespace rp {
	class RetroPlugEcsApplication : public fw::app::Application {
	private:
		RetroPlugProjectPtr _project;
		fw::EventNode::NodeId _audioNodeId = 0;
		std::optional<fw::EventNode> _audioEventNode = std::nullopt;

	public:
		RetroPlugEcsApplication();
		~RetroPlugEcsApplication() = default;

		fw::ViewPtr onCreateUi() override;

		fw::AudioProcessorPtr onCreateAudio() override;

		void onUpdate(f32 deltaTime) override;

		RetroPlugProjectPtr getProject();
	};
}
