#pragma once

#include "application/Application.h"
#include "core/RetroPlugProject.h"

namespace rp {
	class RetroPlugApplication : public fw::app::Application {
	private:
		std::optional<fw::EventNode> _audioEventNode = std::nullopt;
		RetroPlugProject _project;

	public:
		RetroPlugApplication();
		~RetroPlugApplication() = default;

		fw::ViewPtr onCreateUi() override;

		fw::AudioProcessorPtr onCreateAudio() override;

		fw::ViewPtr onCreateNamedView(const std::string& name) override;

		void onUpdate(f32 deltaTime) override;

		RetroPlugProject& getProject() {
			return _project;
		}

		RetroPlugProject* getProjectPtr() {
			return &_project;
		}
	};
}
