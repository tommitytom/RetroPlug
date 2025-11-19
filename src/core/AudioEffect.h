#pragma once

#include <functional>
#include <entt/entity/registry.hpp>

#include "foundation/Replicator.h"
#include "audio/AudioBuffer.h"
#include "core/Components.h"

namespace rp {
	using ComponentLifetimeFunction = std::function<void(entt::registry&, entt::entity)>;

	template <typename Component, typename ComponentLifetimeTag>
	inline bool onLifetimeUpdate(entt::registry& registry, ComponentLifetimeFunction&& func, bool mustExist) {
		auto view = registry.view<ComponentLifetimeTag>();
		if (view.empty()) [[likely]] {
			return false;
		}

		for (const auto [e] : registry.view<ComponentLifetimeTag>().each()) {
			if (!mustExist || registry.all_of<Component>(e)) [[likely]] {
				func(registry, e);
			}
		}

		registry.clear<ComponentLifetimeTag>();

		return true;
	}

	template <typename Component>
	inline bool onCreate(entt::registry& registry, ComponentLifetimeFunction&& func) {
		return onLifetimeUpdate<Component, fw::Replicator::ComponentCreatedTag<Component>>(registry, std::move(func), true);
	}

	template <typename Component>
	inline bool onUpdate(entt::registry& registry, ComponentLifetimeFunction&& func) {
		return onLifetimeUpdate<Component, fw::Replicator::ComponentUpdatedTag<Component>>(registry, std::move(func), true);
	}

	template <typename Component>
	inline bool onDestroy(entt::registry& registry, ComponentLifetimeFunction&& func) {
		return onLifetimeUpdate<Component, fw::Replicator::ComponentDestroyedTag<Component>>(registry, std::move(func), false);
	}

	template <typename Component, typename ...StateComponents>
	inline bool createEffectUpdate(entt::registry& registry, AudioEffectBase* effect) {
		bool changed = false;

		changed |= onCreate<Component>(registry, [effect](entt::registry& registry, entt::entity entity) {
			registry.emplace<AudioProcessorComponent>(entity, effect);
			(registry.emplace<StateComponents>(entity), ...);
		});

		changed |= onDestroy<Component>(registry, [](entt::registry& registry, entt::entity entity) {
			(registry.remove<StateComponents>(entity), ...);
			registry.remove<AudioProcessorComponent>(entity);
		});

		return changed;
	}

	class AudioEffectBase {
	public:
		virtual ~AudioEffectBase() = default;
		virtual bool update(entt::registry& registry) { return false; }
		virtual void process(entt::registry& registry, entt::entity e, fw::AudioBuffer& out, const fw::AudioBuffer& in) = 0;
	};

	template<typename Component, typename... StateComponents>
	class AudioEffect : public AudioEffectBase {
	private:
		entt::registry* _registry = nullptr;

	public:
		using ComponentType = Component;

		AudioEffect() {}
		virtual ~AudioEffect() {}

		static entt::entity emplace(entt::registry& registry, entt::entity entity = entt::null) {
			if (entity == entt::null) {
				entity = fw::Replicator::spawn(registry);
			}
			registry.emplace<Component>(entity, Component{});
			return entity;
		}

		bool update(entt::registry& registry) override {
			return createEffectUpdate<Component, StateComponents...>(registry, this);
		}

		void process(entt::registry& registry, entt::entity e, fw::AudioBuffer& out, const fw::AudioBuffer& in) final override {
			auto comps = registry.get<const Component, StateComponents...>(e);
			std::apply([this](auto&... args) {
				this->process(args...);
			}, std::tuple_cat(std::make_tuple(std::ref(out)), comps));
		}

		virtual void process(fw::AudioBuffer& out, const Component& comp, StateComponents&...) = 0;

		entt::registry& getRegistry() {
			assert(_registry);
			return *_registry;
		}

		const entt::registry& getRegistry() const {
			assert(_registry);
			return *_registry;
		}
	};

	// Same as audioeffect?
	template<typename Component, typename... StateComponents>
	class AudioGenerator : public AudioEffectBase {
	private:
		entt::registry* _registry = nullptr;

	public:
		using ComponentType = Component;

		AudioGenerator() {}
		virtual ~AudioGenerator() {}

		static entt::entity emplace(entt::registry& registry, entt::entity entity = entt::null) {
			if (entity == entt::null) {
				entity = fw::Replicator::spawn(registry);
			}
			registry.emplace<Component>(entity, Component{});
			return entity;
		}

		bool update(entt::registry& registry) override {
			return createEffectUpdate<Component, StateComponents...>(registry, this);
		}

		void process(entt::registry& registry, entt::entity e, fw::AudioBuffer& out, const fw::AudioBuffer& in) final override {
			auto comps = registry.get<const Component, StateComponents...>(e);
			std::apply([this](auto&... args) {
				this->process(args...);
			}, std::tuple_cat(std::make_tuple(std::ref(out)), comps));
		}

		virtual void process(fw::AudioBuffer& out, const Component& comp, StateComponents&...) = 0;

		entt::registry& getRegistry() {
			assert(_registry);
			return *_registry;
		}

		const entt::registry& getRegistry() const {
			assert(_registry);
			return *_registry;
		}
	};
}
