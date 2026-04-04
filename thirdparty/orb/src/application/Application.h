#pragma once

#include "foundation/Event.h"
#include "audio/AudioManager.h"
#include "application/UiContext.h"

namespace orb::app {
	class Application {
	public:
		Application();
		virtual ~Application() = default;

		virtual orb::ViewPtr onCreateUi() { return nullptr; }

		virtual orb::ViewPtr onCreateNamedView(const std::string& name) { return nullptr; }

		virtual orb::AudioProcessorPtr onCreateAudio() { return nullptr; }

		virtual void onUpdate(f32 deltaTime) {}

		virtual void onSerialize(orb::Uint8Buffer& buffer) {}

		virtual void onDeserialize(const orb::Uint8Buffer& buffer) {}
	};

	template <typename ViewT, typename AudioT = NullAudioProcessor>
	class BasicApplication : public Application {
	public:
		orb::ViewPtr onCreateUi() override {
			return std::make_shared<ViewT>();
		}

		orb::AudioProcessorPtr onCreateAudio() override {
			if constexpr (!std::is_same_v<AudioT, void>) {
				return std::make_shared<AudioT>();
			}

			return std::make_shared<orb::NullAudioProcessor>();
		}
	};
}
