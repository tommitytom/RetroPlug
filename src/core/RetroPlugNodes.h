#pragma once

#include "node/Node.h"
#include "util/DataBuffer.h"
#include "core/System.h"
#include "node/AudioGraph.h"

namespace rp {
	constexpr f32 PI = 3.14159265359f;
	constexpr f32 PI2 = PI * 2;

	

	struct SwapSystemMessage {
		SystemPtr system;
	};

	using SystemMessage = std::variant<SwapSystemMessage>;

	class SystemNodeProcessor : public NodeProcessor<SystemMessage> {
	private:
		SystemPtr _system;

	public:
		void onInitialize() override {
			addOutput<SystemPtr>("System", nullptr);
		}

		void onProcess() override {
			processMessages(overload {
				[&](SwapSystemMessage& val) { 
					if (val.system) {
						_system = std::move(val.system);
					} else {
						val.system = std::move(_system);
					}

					setOutputValue(0, _system);
				},
			});
		}
	};

	class SystemNode : public Node<SystemNodeProcessor> {
	public:
		SystemNode() {

		}
	};

	struct SetBufferMessage { AudioBuffer buffer; };

	class AudioBufferProcessor : public NodeProcessor<std::variant<SetBufferMessage>> {
	public:
		AudioBufferProcessor() {
			addOutput<AudioBuffer>("Out");
			setAlwaysActive(true);
		}

		void onProcess() override {
			processMessages(overload {
				[&](SetBufferMessage& val) {
					setOutputValue(0, std::move(val.buffer));
				}
			});
		}
	};

	class AudioBufferNode : public Node<AudioBufferProcessor> {
	public:
		void setBuffer(AudioBuffer&& buffer) {
			this->sendMessage(SetBufferMessage { std::move(buffer) });
		}
	};

	

	class SineProcessor : public AudioNodeProcessor<> {
	private:
		f32 _phase = 0.0f;

	public:
		SineProcessor() {
			addInput<f32>("Freq", 400.0f);
			addAudioOutput("Out");
		}

		void onProcess() override {
			AudioBuffer out = getAudioOutput(0);
			f32 freq = getInputValue<f32>(0);
			f32 step = 0.05f;// (freq / getSampleRate())* PI2;

			for (size_t i = 0; i < out.size(); ++i) {
				out[i] = sin(_phase);
				_phase = fmod(_phase + step, PI2);
				out[i] = 0.0f;
			}
		}
	};

	class SineNode : public Node<SineProcessor> {};


	class AddProcessor : public NodeProcessor<> {
	public:
		AddProcessor() {
			addInput<AudioBuffer>("A");
			addInput<AudioBuffer>("B");
			addOutput<AudioBuffer>("Out", AudioBuffer(1024));
		}

		void onProcess() override {
			const AudioBuffer& a = getInputValue<AudioBuffer>(0);
			const AudioBuffer& b = getInputValue<AudioBuffer>(1);
			AudioBuffer& out = getOutputValue<AudioBuffer>(0);

			out.write(a);
			out.add(b);			
		}
	};

	class AddNode : public Node<AddProcessor> {};
}
