#pragma once

#include "foundation/Event.h"
#include "audio/AudioManager.h"
#include "application/UiContext.h"
#include "ui/EditOverlay.h"

namespace fw::app {
	class Application {
	public:
		Application();
		~Application() = default;

		virtual fw::ViewPtr onCreateUi() { return nullptr; }

		virtual fw::AudioProcessorPtr onCreateAudio() { return nullptr; }
	};

	template <typename ViewT, typename AudioT = NullAudioProcessor>
	class BasicApplication : public Application {
	public:
		fw::ViewPtr onCreateUi() override {
			if constexpr (!std::is_same_v<ViewT, void>) {
				ViewPtr view = std::make_shared<ViewT>();
				EditViewPtr editView = std::make_shared<EditView>();
				editView->setView(view);
				editView->getLayout().setDimensions(view->getDimensions());
				return editView;
			}

			return nullptr;
		}

		fw::AudioProcessorPtr onCreateAudio() override {
			if constexpr (!std::is_same_v<AudioT, void>) {
				return std::make_shared<AudioT>();
			}
			
			return std::make_shared<fw::NullAudioProcessor>();
		}
	};
}
