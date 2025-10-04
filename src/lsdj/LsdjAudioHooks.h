#pragma once

#include "core/SystemHook.h"

namespace rp {
	class LsdjAudioHooks : public AudioSystemHook {
	public:
		LsdjAudioHooks(entt::id_type systemType) : AudioSystemHook(systemType) {}
		virtual ~LsdjAudioHooks() {}
		virtual void onTransportChange(entt::registry& registry, entt::entity entity, bool running) const override;
		virtual void onTransportUpdate(entt::registry& registry, entt::entity entity, const fw::TimeInfo& timeInfo) const override;
		virtual void onMidi(entt::registry& registry, entt::entity entity, const fw::MidiMessage& message) const override;
		virtual void onMidiClock(entt::registry& registry, entt::entity entity) const override;
	};
}
