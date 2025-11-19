#pragma once

#include <functional>
#include <entt/entity/registry.hpp>
#include "ui/View.h"
#include "core/ProjectSerializerContext.h"
#include "core/CoreComponents.h"
#include "audio/TimeInfo.h"
#include "audio/MidiMessage.h"

namespace rp {
	using PathVector = std::vector<std::filesystem::path>;

	struct NamedEntry {
		std::string type;
		std::filesystem::path path;
		orb::Uint8Buffer data;
	};
	using NamedEntryVector = std::vector<NamedEntry>;

	inline NamedEntry* findEntry(NamedEntryVector& entries, const std::string& type) {
		for (NamedEntry& entry : entries) {
			if (entry.type == type) {
				return &entry;
			}
		}

		return nullptr;
	}

	inline void filterEntries(const PathVector& paths, NamedEntryVector& out, const std::string& ext, const std::string& type) {
		for (const std::filesystem::path& path : paths) {
			if (orb::StringUtil::toLower(path.extension().string()) == ext) {
				out.push_back({ type, path });
			}
		}
	}

	class HookBase {
	private:
		entt::id_type _systemType;

	public:
		HookBase(entt::id_type systemType) : _systemType(systemType) {}
		virtual ~HookBase() {}

		entt::id_type getType() const {
			return _systemType;
		}
	};

	class SystemHookBase : public HookBase {
	public:
		SystemHookBase(entt::id_type systemType) : HookBase(systemType) {}

		virtual void onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const {}

		virtual void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const {}

		virtual void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const {}

		virtual void onAfterLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const {}

		virtual void onReset(entt::registry& registry, entt::entity entity) const {}

		virtual void onDestroy(entt::registry& registry, entt::entity entity) const {}

		virtual orb::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity) const { return nullptr; }

		virtual void onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const {}

		virtual void onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const {}

		virtual void onMoveComponents(entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) const {}

		virtual void onReplicate(entt::registry& registry) const {}

		virtual std::string onGetSystemName(const entt::registry& registry, entt::entity entity) const { return ""; }
	};

	using HooksVector = std::vector<SystemHookBase*>;

	template <typename SystemComponent>
	class SystemHook : public SystemHookBase {
	public:
		SystemHook() : SystemHookBase(entt::type_id<SystemComponent>().index()) {}

		void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const override {
			onBeforeLoad(registry, entity, load, registry.get<SystemComponent>(entity));
		}

		virtual void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SystemComponent& component) const {}

		void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const override {
			onLoad(registry, entity, load, registry.get<SystemComponent>(entity));
		}

		virtual void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SystemComponent& system) const {}

		void onAfterLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load) const override {
			onAfterLoad(registry, entity, load, registry.get<SystemComponent>(entity));
		}

		virtual void onAfterLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SystemComponent& component) const {}

		void onReset(entt::registry& registry, entt::entity entity) const override {
			onReset(registry, entity, registry.get<SystemComponent>(entity));
		}

		virtual void onReset(entt::registry& registry, entt::entity entity, SystemComponent& component) const {}

		void onDestroy(entt::registry& registry, entt::entity entity) const override {
			onDestroy(registry, entity, registry.get<SystemComponent>(entity));
		}

		virtual void onDestroy(entt::registry& registry, entt::entity entity, SystemComponent& component) const {}

		orb::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity) const override {
			return onCreateOverlay(registry, entity, registry.get<SystemComponent>(entity));
		}

		virtual orb::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity, SystemComponent& system) const { return nullptr; }
	};

	class AudioSystemHook : public HookBase {
	public:
		AudioSystemHook(entt::id_type systemType): HookBase(systemType) {}
		virtual ~AudioSystemHook() {}

		virtual void onSaveSram(entt::registry& registry, entt::entity entity, orb::Uint8Buffer& target) const {}
		virtual void onSaveState(entt::registry& registry, entt::entity entity, orb::Uint8Buffer& target) const {}
		virtual MemoryAccessor onGetMemory(entt::registry& registry, entt::entity entity, MemoryType type, AccessType access) const { return MemoryAccessor(); }
		virtual void onPatchMemory(entt::registry& registry, entt::entity entity, const MemoryPatch& patch) const {}
		virtual void onReset(entt::registry& registry, entt::entity entity) const {}
		virtual void onTransportChange(entt::registry& registry, entt::entity entity, bool running) const {}
		virtual void onTransportUpdate(entt::registry& registry, entt::entity entity, const orb::TimeInfo& timeInfo) const {}
		virtual void onMidi(entt::registry& registry, entt::entity entity, const orb::MidiMessage& message) const {}
		virtual void onMidiClock(entt::registry& registry, entt::entity entity) const {}
	};

	using AudioHooksVector = std::vector<AudioSystemHook*>;

	inline void eachHook(entt::id_type systemType, const HooksVector& hooks, std::function<void(const SystemHookBase&)>&& func) {
		for (const SystemHookBase* hook : hooks) {
			if (hook->getType() == systemType) {
				func(*hook);
			}
		}
	}

	inline void eachHook(const HooksVector& hooks, std::function<void(const SystemHookBase&)>&& func) {
		for (const SystemHookBase* hook : hooks) {
			func(*hook);
		}
	}

	inline void eachHook(const AudioHooksVector& hooks, std::function<void(const AudioSystemHook&)>&& func) {
		for (const AudioSystemHook* hook : hooks) {
			func(*hook);
		}
	}

	inline const SystemHookBase* findHook(entt::id_type systemType, const HooksVector& hooks) {
		for (const SystemHookBase* hook : hooks) {
			if (hook->getType() == systemType) {
				return hook;
			}
		}
		return nullptr;
	}

	inline AudioSystemHook* findHook(entt::id_type systemType, const std::vector<std::unique_ptr<AudioSystemHook>>& hooks) {
		for (const std::unique_ptr<AudioSystemHook>& hook : hooks) {
			if (hook->getType() == systemType) {
				return static_cast<AudioSystemHook*>(hook.get());
			}
		}
		return nullptr;
	}
}
