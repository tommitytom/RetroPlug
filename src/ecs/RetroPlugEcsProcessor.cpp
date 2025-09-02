#include "RetroPlugEcsProcessor.h"

#include "foundation/Replicator.h"
#include "audio/AudioBuffer.h"
#include "Components.h"
#include "AudioEffect.h"
#include "SineGenerator.h"
#include "sameboy/SameBoyComponents.h"
#include "sameboy/SameBoyUtil.h"
#include "foundation/FsUtil.h"
#include "ecs/RetroPlugComponents.h"
#include "ecs/HierarchyUtil.h"

namespace rp {
	using namespace entt::literals;

	struct AudioSettingsContext {
		f32 sampleRate = 48000.0f;
		uint32 blockSize = 512;
	};

	RetroPlugEcsProcessor::RetroPlugEcsProcessor(fw::EventNode&& eventNode) : fw::AudioProcessor(std::move(eventNode))  {
		_registry.ctx().emplace<AudioSettingsContext>();
		AudioEffectContext& effectCtx = _registry.ctx().emplace<AudioEffectContext>();

		effectCtx.effects.push_back(std::make_unique<SineGenerator>());

		fw::EventNode& node = getEventNode();
		fw::Replicator::setupOwner(_registry, node);
		fw::Replicator::replicate<ReplicatedTypes>(_registry);

		node.receive<ButtonEvent>([this](ButtonEvent&& ev) {
			SameBoyStateComponent* state = _registry.try_get<SameBoyStateComponent>(ev.entity);
			state->io->input.buttons.push_back(fw::StreamButtonPress{
				.button = (fw::ButtonType)ev.button,
				.down = ev.down
			});
		});
	}

	RetroPlugEcsProcessor::~RetroPlugEcsProcessor() {
		fw::Replicator::shutdown(_registry);
	}

	void RetroPlugEcsProcessor::onTransportChange(bool playing) {
	}

	void RetroPlugEcsProcessor::onTransportUpdate(const fw::TimeInfo& timeInfo) {
	}

	void RetroPlugEcsProcessor::onBeginUpdate(uint32 frameCount) {
		fw::Replicator::beginUpdate(_registry);
		getEventNode().update();
		fw::Replicator::endUpdate(_registry);
	}

	void RetroPlugEcsProcessor::onRenderFull(fw::AudioBuffer& out, const fw::AudioBuffer& in) {
		AudioSettingsContext& settings = _registry.ctx().at<AudioSettingsContext>();
		settings.sampleRate = out.getSampleRate();
		settings.blockSize = out.getSampleCount();

		onCreate<SameBoyStateComponent>(_registry, [](entt::registry& registry, entt::entity entity) {
			const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();
			SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
			SameBoyUtil::setUserData(state, (void*)&state);
			SameBoyUtil::setSampleRate(state, (uint32)settings.sampleRate);
			state.io = std::make_shared<SystemIo>();
		});

		onDestroy<SameBoyStateComponent>(_registry, [](entt::registry& registry, entt::entity entity) {
			SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
			SameBoyUtil::destroy(state);
			HierarchyUtil::destroyHierarchy(registry, entity, false);
			registry.remove<SameBoyStateComponent>(entity);
		});

		auto view = _registry.view<SameBoyStateComponent>();
		const size_t systemCount = view.size();
		if (!systemCount) {
			return;
		}

		for (const auto& [e, state] : view.each()) {
			state.io = state.io ? state.io : std::make_shared<SystemIo>();
			state.io->output.audio = std::make_shared<fw::Float32Buffer>(settings.blockSize * 2);
		}

		SameBoyUtil::process(view.storage().raw(), view.size(), settings.blockSize);

		f32* outL = out.getWritePointer(0);
		f32* outR = out.getWritePointer(1);

		for (const auto& [e, state] : view.each()) {
			const f32* buffer = state.io->output.audio->data();
			for (uint32 i = 0; i < settings.blockSize; ++i) {
				outL[i] += buffer[i * 2 + 0];
				outR[i] += buffer[i * 2 + 1];
			}

			state.io->output.audio = nullptr;

			if (state.io->output.video) {
				getEventNode().trySend("Ui"_hs, SystemIoEvent{
					.entity = e,
					.io = std::move(state.io)
				});

				state.io = std::make_shared<SystemIo>();
			}
		}
	}

	void RetroPlugEcsProcessor::onRender(f32* output, const f32* input, uint32 frameCount) {
		AudioEffectContext& effectCtx = _registry.ctx().emplace<AudioEffectContext>();
		fw::AudioBuffer outBuffer(2, frameCount, getSampleRate());
		fw::AudioBuffer inBuffer;

		outBuffer.clear();

		if (input) {
			inBuffer.fromInterleaved(input, 2, frameCount);
		} else {
			inBuffer.resize(2, frameCount);
			inBuffer.clear();
		}

		onRenderFull(outBuffer, inBuffer);
		outBuffer.toInterleaved(output, 2, frameCount);

		return;

		for (auto& effect : effectCtx.effects) {
			effect->update(_registry);
		}

		for (const auto& [e, effect] : _registry.view<AudioProcessorComponent>().each()) {
			effect.effect->process(_registry, e, outBuffer, inBuffer);
		}

		outBuffer.toInterleaved(output, 2, frameCount);
	}

	void RetroPlugEcsProcessor::onMidi(const fw::MidiMessage& message) {
	}

	void RetroPlugEcsProcessor::onSampleRateChange(f32 sampleRate) {
	}

	void RetroPlugEcsProcessor::onSerialize(fw::Uint8Buffer& target) {
		// Thread safe? Who knows!
		if (_serializeHook) {
			_serializeHook(target);
		}
	}

	void RetroPlugEcsProcessor::onDeserialize(const fw::Uint8Buffer& source) {
		// Thread safe? Who knows!
		if (_deserializeHook) {
			_deserializeHook(source);
		}
	}
}
