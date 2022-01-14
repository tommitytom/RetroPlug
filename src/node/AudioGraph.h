#pragma once

#include "util/DataBuffer.h"
#include "NodeGraphProcessor.h"
#include "NodeGraph.h"
#include "AudioBuffer.h"

namespace rp {
	struct AudioEngineState {
		f32 sampleRate = 48000;
		uint32 blockSize = 0;
		uint32 currentFrameCount = 0;
	};

	template <typename Message = std::variant<std::monostate>>
	class AudioNodeProcessor : public NodeProcessor<Message> {
	public:
		f32 getSampleRate() const {
			return this->getState<AudioEngineState>().sampleRate;
		}

		uint32 getBlockSize() const {
			return this->getState<AudioEngineState>().blockSize;
		}

		uint32 getFrameCount() const {
			return this->getState<AudioEngineState>().currentFrameCount;
		}

		const Input<AudioBuffer>* addAudioInput(std::string_view name) {
			return this->addInput<AudioBuffer>(name);
		}

		Output<AudioBuffer>* addAudioOutput(std::string_view name) {
			return this->addOutput<AudioBuffer>(name);
		}

		AudioBuffer getAudioOutput(size_t idx) {
			return this->getOutputValue<AudioBuffer>(idx).slice(0, getFrameCount());
		}

		AudioBuffer getAudioInput(size_t idx) {
			return this->getInputValue<AudioBuffer>(idx).slice(0, getFrameCount());
		}
	};

	template <typename Message = std::variant<std::monostate>>
	class AudioNode : public Node<Message> {};

	class OutputProcessor final : public AudioNodeProcessor<> {
	public:
		OutputProcessor() {
			addAudioInput("In");
		}
	};

	class OutputNode final : public AudioNode<OutputProcessor> {};

	class AudioGraphProcessor : public NodeGraphProcessor {
	public:
		AudioGraphProcessor() {
			addState<AudioEngineState>();
		}

		void setSampleRate(f32 sampleRate) {
			getState<AudioEngineState>().sampleRate = sampleRate;
		}

		void setBlockSize(uint32 blockSize) {
			getState<AudioEngineState>().blockSize = blockSize;

			// Resize output buffers
			for (NodeProcessorPtr& processor : getNodes()) {
				for (OutputBase& output : processor->getOutputs()) {
					if (output.data.type() == entt::type_id<AudioBuffer>()) {
						auto buffer = static_cast<AudioBuffer*>(output.data.data());
						buffer->resize(blockSize);
					}
				}
			}
		}

		void process(f32* output, const f32* input, uint32 frameCount) {
			handleMessageProcessing();

			if (frameCount > getState<AudioEngineState>().blockSize) {
				setBlockSize(frameCount);
			}

			getState<AudioEngineState>().currentFrameCount = frameCount;

			processNodes();

			// Find output node
			/*for (NodeProcessorPtr& processor : getNodes()) {
				for (OutputBase& output : processor->getOutputs()) {
					if (output.data.type() == entt::type_id<AudioBuffer>()) {
						auto buffer = static_cast<AudioBuffer*>(output.data.data());
						buffer->resize(blockSize);
					}
				}
			}*/
		}
	};

	class AudioGraph : public NodeGraph<AudioGraphProcessor> {
	public:

	};
}
