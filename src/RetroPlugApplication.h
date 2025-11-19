#pragma once

#include "application/Application.h"
#include "core/RetroPlugProject.h"

namespace rp {
	class RetroPlugApplication : public orb::app::Application {
	private:
		std::optional<orb::EventNode> _audioEventNode = std::nullopt;
		RetroPlugProject _project;

	public:
		RetroPlugApplication();
		~RetroPlugApplication() = default;

		orb::ViewPtr onCreateUi() override;

		orb::AudioProcessorPtr onCreateAudio() override;

		orb::ViewPtr onCreateNamedView(const std::string& name) override;

		void onUpdate(f32 deltaTime) override;

		RetroPlugProject& getProject() {
			return _project;
		}

		RetroPlugProject* getProjectPtr() {
			return &_project;
		}
	};
}
