#pragma once

#include "foundation/Event.h"
#include "audio/AudioManager.h"
#include "application/UiContext.h"

namespace fw::app {
	class Application {
	private:
		fw::AudioProcessorPtr _audioProcessor;
		fw::ViewPtr _view;

	public:
		Application();
		~Application() = default;

		virtual fw::ViewPtr onCreateUi() { return nullptr; }

		virtual fw::AudioProcessorPtr onCreateAudio() { return nullptr; }
	};

	template <typename ViewT, typename AudioT>
	class BasicApplication : public Application {
	public:
		fw::ViewPtr onCreateUi() override {
			if constexpr (!std::is_same_v<ViewT, void>) {
				return std::make_shared<ViewT>();
			}

			return nullptr;
		}

		fw::AudioProcessorPtr onCreateAudio() override {
			if constexpr (!std::is_same_v<AudioT, void>) {
				return std::make_shared<AudioT>();
			}

			return nullptr;
		}
	};
}
