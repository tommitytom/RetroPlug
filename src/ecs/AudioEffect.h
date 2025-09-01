#pragma once

#include <functional>
#include <entt/entity/registry.hpp>
#include "audio/AudioBuffer.h"
#include "foundation/Replicator.h"

namespace rp {
	using ComponentLifetimeFunction = std::function<void(entt::registry&, entt::entity)>;

	template <typename ComponentLifetimeTag>
	inline bool onLifetimeUpdate(entt::registry& registry, ComponentLifetimeFunction&& func) {
		auto view = registry.view<ComponentLifetimeTag>();
		if (view.empty()) {
			return false;
		}

		for (const auto [e] : registry.view<ComponentLifetimeTag>().each()) {
			func(registry, e);
		}

		registry.clear<ComponentLifetimeTag>();

		return true;
	}

	template <typename Component>
	inline bool onCreate(entt::registry& registry, ComponentLifetimeFunction&& func) {
		return onLifetimeUpdate<fw::Replicator::ComponentCreatedTag<Component>>(registry, std::move(func));
	}

	template <typename Component>
	inline bool onDestroy(entt::registry& registry, ComponentLifetimeFunction&& func) {
		return onLifetimeUpdate<fw::Replicator::ComponentDestroyedTag<Component>>(registry, std::move(func));
	}

	template <typename Component, typename ...StateComponents>
	inline bool createEffectUpdate(entt::registry& registry, AudioEffectBase* effect) {
		bool changed = onCreate<Component>(registry, [effect](entt::registry& registry, entt::entity entity) {
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
	public:
		using ComponentType = Component;

		AudioEffect() {}
		virtual ~AudioEffect() {}

		static entt::entity emplace(entt::registry& registry, entt::entity entity) {
			registry.emplace<Component>(entity, Component{});
			return entity;
		}

		bool update(entt::registry& registry) override {
			return createEffectUpdate<Component, StateComponents...>(registry, this);
		}

		void process(entt::registry& registry, entt::entity e, fw::AudioBuffer& out, const fw::AudioBuffer& in) final override {
			auto comps = registry.get<const Component, StateComponents...>(e);
			std::apply([this](auto&... args) {
				this->processTyped(args...);
			}, std::tuple_cat(std::make_tuple(std::ref(out), std::cref(in)), comps));
		}

		virtual void processTyped(fw::AudioBuffer& out, const fw::AudioBuffer& in, const Component& comp, StateComponents&...) = 0;
	};

	template<typename Component, typename... StateComponents>
	class AudioGenerator : public AudioEffectBase {
	public:
		using ComponentType = Component;

		AudioGenerator() {}
		virtual ~AudioGenerator() {}

		static entt::entity emplace(entt::registry& registry, entt::entity entity) {
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
	};
}
