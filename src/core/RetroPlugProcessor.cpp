#include "RetroPlugProcessor.h"

#include <spdlog/spdlog.h>

#include "sameboy/SameBoyAudioHooks.h"
#include "sameboy/SameBoyComponents.h"
#include "sameboy/SameBoyUtil.h"
#include "foundation/FsUtil.h"
#include "foundation/Replicator.h"
#include "audio/AudioBuffer.h"
#include "core/Events.h"
#include "core/HierarchyUtil.h"
#include "core/RetroPlugComponents.h"
#include "core/SystemHook.h"
#include "lsdj/LsdjComponents.h"

#include "AudioEffect.h"
#include "Components.h"
#include "SineGenerator.h"

namespace rp {
	using namespace entt::literals;

	struct AudioSettingsContext {
		f32 sampleRate = 48000.0f;
		uint32 blockSize = 512;
	};

	struct AudioHooksContext {
		std::vector<std::unique_ptr<AudioSystemHook>> systemHooks;
		std::vector<std::unique_ptr<AudioSystemHook>> serviceHooks;

		// Delete copy operations
		AudioHooksContext(const AudioHooksContext&) = delete;
		AudioHooksContext& operator=(const AudioHooksContext&) = delete;

		// Keep move operations (automatically generated)
		AudioHooksContext(AudioHooksContext&&) = default;
		AudioHooksContext& operator=(AudioHooksContext&&) = default;

		AudioHooksContext() = default;
		~AudioHooksContext() = default;
	};

	fw::ButtonType convertButtonType(fw::PadButtonType button) {
		switch (button) {
			case fw::PadButtonType::A: return fw::ButtonType::A;
			case fw::PadButtonType::B: return fw::ButtonType::B;
			case fw::PadButtonType::Select: return fw::ButtonType::Select;
			case fw::PadButtonType::Start: return fw::ButtonType::Start;
			case fw::PadButtonType::Up: return fw::ButtonType::Up;
			case fw::PadButtonType::Down: return fw::ButtonType::Down;
			case fw::PadButtonType::Left: return fw::ButtonType::Left;
			case fw::PadButtonType::Right: return fw::ButtonType::Right;
			default: return fw::ButtonType::MAX;
		}
	}

	RetroPlugProcessor::RetroPlugProcessor(fw::EventNode&& eventNode) : fw::AudioProcessor(std::move(eventNode))  {
		AudioHooksContext& hooks = _registry.ctx().emplace<AudioHooksContext>();
		hooks.systemHooks.push_back(std::make_unique<SameBoyAudioHooks>());

		_registry.ctx().emplace<AudioSettingsContext>();
		AudioEffectContext& effectCtx = _registry.ctx().emplace<AudioEffectContext>();

		effectCtx.effects.push_back(std::make_unique<SineGenerator>());

		fw::EventNode& node = getEventNode();
		fw::Replicator::setupOwner(_registry, node);
		fw::Replicator::replicate<ReplicatedTypes>(_registry);

		node.receive<PadButtonEvent>([this](PadButtonEvent&& ev) {
			fw::ButtonType button = convertButtonType(ev.button);
			if (button == fw::ButtonType::MAX) {
				return;
			}

			if (!_registry.valid(ev.entity)) {
				return;
			}

			SameBoyStateComponent* state = _registry.try_get<SameBoyStateComponent>(ev.entity);
			if (!state) {
				return;
			}

			state->state->io->input.buttons.push_back(fw::StreamButtonPress{
				.button = button,
				.down = ev.down
			});
		});

		node.receive<FetchMemoryRequest>([this](FetchMemoryRequest&& ev){
			if (!_registry.valid(ev.entity)) {
				return;
			}

			SystemComponent* system = _registry.try_get<SystemComponent>(ev.entity);
			if (!system) {
				return;
			}

			AudioHooksContext& hooks = _registry.ctx().at<AudioHooksContext>();
			AudioSystemHook* hook = findHook(system->systemType, hooks.systemHooks);
			assert(hook);

			fw::Uint8Buffer target;

			if (ev.type == MemoryType::MAX) {
				hook->onSaveState(_registry, ev.entity, target);
			} else {
				MemoryAccessor memory = hook->onGetMemory(_registry, ev.entity, ev.type, AccessType::Read);
				target.resize(memory.getSize());
				target.write(memory.getBuffer());
			}

			if (!target.empty()) {
				getEventNode().trySend("Ui"_hs, FetchMemoryResponse{
					.entity = ev.entity,
					.type = ev.type,
					.state = std::move(target)
				});
			}
		});

		node.receive<MemoryPatchEvent>([this](MemoryPatchEvent&& ev) {
			if (!_registry.valid(ev.entity)) {
				spdlog::warn("Received MemoryPatchEvent for invalid entity {}", (size_t)ev.entity);
				return;
			}

			SystemComponent* system = _registry.try_get<SystemComponent>(ev.entity);
			if (!system) {
				return;
			}

			AudioHooksContext& hooks = _registry.ctx().at<AudioHooksContext>();
			AudioSystemHook* hook = findHook(system->systemType, hooks.systemHooks);
			assert(hook);

			for (const MemoryPatch& patch : ev.patches) {
				hook->onPatchMemory(_registry, ev.entity, patch);
			}
		});

		node.receive<PingEvent>([&](PingEvent&& ev) {
			node.trySend("Ui"_hs, PongEvent{ .time = ev.time });
		});

		node.receive<ResetSystemEntityEvent>([&](ResetSystemEntityEvent&& ev) {
			if (!_registry.valid(ev.entity)) {
				return;
			}
			SystemComponent* system = _registry.try_get<SystemComponent>(ev.entity);
			if (!system) {
				return;
			}

			AudioHooksContext& hooks = _registry.ctx().at<AudioHooksContext>();
			AudioSystemHook* hook = findHook(system->systemType, hooks.systemHooks);
			assert(hook);
			hook->onReset(_registry, ev.entity);
		});
	}

	RetroPlugProcessor::~RetroPlugProcessor() {
		fw::Replicator::shutdown(_registry);
	}

	void RetroPlugProcessor::onTransportChange(bool playing) {
	}

	void RetroPlugProcessor::onTransportUpdate(const fw::TimeInfo& timeInfo) {
	}

	void RetroPlugProcessor::onBeginUpdate(uint32 frameCount) {
		fw::Replicator::beginUpdate(_registry);
		getEventNode().update();
		fw::Replicator::endUpdate(_registry);
	}

	void RetroPlugProcessor::onRenderFull(fw::AudioBuffer& out, const fw::AudioBuffer& in) {
		AudioSettingsContext& settings = _registry.ctx().at<AudioSettingsContext>();
		settings.sampleRate = out.getSampleRate();
		settings.blockSize = out.getSampleCount();

		onCreate<SameBoyStateComponent>(_registry, [](entt::registry& registry, entt::entity entity) {
			const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();
			SameBoyState& state = *registry.get<SameBoyStateComponent>(entity).state;
			SameBoyUtil::setSampleRate(state, (uint32)settings.sampleRate);
			state.io = std::make_shared<SystemIo>();
		});

		onDestroy<SameBoyStateComponent>(_registry, [](entt::registry& registry, entt::entity entity) {
			SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
			state.state = nullptr;
			HierarchyUtil::destroyHierarchy(registry, entity, false);
			registry.remove<SameBoyStateComponent>(entity);
		});

		auto view = _registry.view<SameBoyStateComponent>();
		const size_t systemCount = view.size();
		if (!systemCount) {
			return;
		}

		std::array<SameBoyStateComponent*, 4> comps;

		uint32 i = 0;
		for (const auto& [e, s] : view.each()) {
			SameBoyState& state = *s.state;
			state.io = state.io ? state.io : std::make_shared<SystemIo>();
			state.io->output.audio = std::make_shared<fw::Float32Buffer>(settings.blockSize * 2);
			state.io->output.audio->clear();

			comps[i++] = &s;
		}

		SameBoyUtil::process(comps.data(), view.size(), settings.blockSize);

		f32* outL = out.getWritePointer(0);
		f32* outR = out.getWritePointer(1);

		for (const auto& [e, s] : view.each()) {
			SameBoyState& state = *s.state;
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

	void RetroPlugProcessor::onRender(f32* output, const f32* input, uint32 frameCount) {
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

	void RetroPlugProcessor::onMidi(const fw::MidiMessage& message) {
	}

	void RetroPlugProcessor::onSampleRateChange(f32 sampleRate) {
	}
}
