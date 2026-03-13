#include "RetroPlugProcessor.h"

#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "foundation/Replicator.h"
#include "audio/AudioBuffer.h"
#include "core/Events.h"
#include "core/HierarchyUtil.h"
#include "core/RetroPlugComponents.h"
#include "core/SystemHook.h"

#include "sameboy/SameBoyAudioHooks.h"
#include "mesen/MesenAudioHooks.h"

#include "lsdj/LsdjComponents.h"

#include "AudioEffect.h"
#include "Components.h"
#include "SineGenerator.h"
#include "AudioSettingsContext.h"

namespace rp {
	using namespace entt::literals;

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

	orb::ButtonType convertButtonType(orb::PadButtonType button) {
		switch (button) {
			case orb::PadButtonType::A: return orb::ButtonType::A;
			case orb::PadButtonType::B: return orb::ButtonType::B;
			case orb::PadButtonType::Select: return orb::ButtonType::Select;
			case orb::PadButtonType::Start: return orb::ButtonType::Start;
			case orb::PadButtonType::Up: return orb::ButtonType::Up;
			case orb::PadButtonType::Down: return orb::ButtonType::Down;
			case orb::PadButtonType::Left: return orb::ButtonType::Left;
			case orb::PadButtonType::Right: return orb::ButtonType::Right;
			default: return orb::ButtonType::MAX;
		}
	}

	RetroPlugProcessor::RetroPlugProcessor(orb::EventNode&& eventNode) : orb::AudioProcessor(std::move(eventNode))  {
		AudioHooksContext& hooks = _registry.ctx().emplace<AudioHooksContext>();
		hooks.systemHooks.push_back(std::make_unique<SameBoyAudioHooks>());
		hooks.systemHooks.push_back(std::make_unique<MesenAudioHooks>());

		_registry.ctx().emplace<AudioSettingsContext>();
		AudioEffectContext& effectCtx = _registry.ctx().emplace<AudioEffectContext>();

		effectCtx.effects.push_back(std::make_unique<SineGenerator>());

		orb::EventNode& node = getEventNode();
		orb::Replicator::setupOwner(_registry, node);
		orb::Replicator::replicate<ReplicatedTypes>(_registry);

		node.receive<PadButtonEvent>([this](PadButtonEvent&& ev) {
			orb::ButtonType button = convertButtonType(ev.button);
			if (button == orb::ButtonType::MAX) {
				return;
			}

			if (!_registry.valid(ev.entity)) {
				return;
			}

			SystemIoComponent* state = _registry.try_get<SystemIoComponent>(ev.entity);
			if (!state) {
				return;
			}

			state->io->input.buttons.push_back(orb::StreamButtonPress{
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

			orb::Uint8Buffer target;

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
		orb::Replicator::shutdown(_registry);
	}

	void RetroPlugProcessor::onTransportChange(bool playing) {
	}

	void RetroPlugProcessor::onTransportUpdate(const orb::TimeInfo& timeInfo) {
	}

	void RetroPlugProcessor::onBeginUpdate(uint32 frameCount) {
		orb::Replicator::beginUpdate(_registry);
		getEventNode().update();
		orb::Replicator::endUpdate(_registry);
	}

	void RetroPlugProcessor::onRenderFull(orb::AudioBuffer& out, const orb::AudioBuffer& in) {
		AudioSettingsContext& settings = _registry.ctx().at<AudioSettingsContext>();
		settings.sampleRate = out.getSampleRate();
		settings.blockSize = out.getSampleCount();

		_registry.view<SystemComponent>(entt::exclude_t<SystemIoComponent>()).each([&](entt::entity e, const SystemComponent& system) {
			_registry.emplace<SystemIoComponent>(e, std::make_shared<SystemIo>());
		});

		for (const auto& [e, state] : _registry.view<SystemIoComponent>().each()) {
			if (!state.io) {
				state.io = std::make_shared<SystemIo>();
			}

			state.io->output.audio = std::make_shared<orb::Float32Buffer>(settings.blockSize * 2);
			state.io->output.audio->clear();
		}

		AudioHooksContext& hooks = _registry.ctx().at<AudioHooksContext>();
		for (const auto& hook : hooks.systemHooks) {
			hook->onProcess(_registry, out, in);
		}

		f32* outL = out.getWritePointer(0);
		f32* outR = out.getWritePointer(1);

		for (const auto& [e, state] : _registry.view<SystemIoComponent>().each()) {
			const f32* buffer = state.io->output.audio->data();
			for (uint32 i = 0; i < settings.blockSize; ++i) {
				outL[i] += buffer[i * 2 + 0];
				outR[i] += buffer[i * 2 + 1];
			}

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
		orb::AudioBuffer outBuffer(2, frameCount, getSampleRate());
		orb::AudioBuffer inBuffer;

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

	void RetroPlugProcessor::onMidi(const orb::MidiMessage& message) {
	}

	void RetroPlugProcessor::onSampleRateChange(f32 sampleRate) {
	}
}
