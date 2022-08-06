#pragma once

#include <unordered_map>

#include <entt/fwd.hpp>
#include <spdlog/spdlog.h>

#include "ResourceHandle.h"
#include "ResourceProvider.h"

namespace rp {
	class ResourceManager {
	private:
		std::unordered_map<entt::id_type, std::unique_ptr<ResourceProvider>> _providers;
		std::unordered_map<UriHash, ResourceHandle> _resources;

	public:
		ResourceManager() = default;
		~ResourceManager() = default;

		template <typename ResourceT, typename ProviderT>
		void addProvider() {
			_providers[entt::type_id<ResourceT>().index()] = std::make_unique<ProviderT>();
		}

		ResourceHandle load(std::string_view uri, entt::id_type resourceType) {
			auto found = _providers.find(resourceType);

			if (found != _providers.end()) {
				std::unique_ptr<Resource> res = found->second->load(uri);

				if (res) {
					std::shared_ptr<ResourceHandleState> state = std::make_shared<ResourceHandleState>();
					state->resource = std::move(res);
					state->uri = std::string(uri);
					state->loaded = true;

					_resources[entt::hashed_string(uri.data())] = ResourceHandle(state);

					return ResourceHandle(state);
				}
			} else {
				spdlog::error("Failed to load resource {}. A resource provider for type {} could not be found", resourceType);
			}
			
			return ResourceHandle();
		}

		template <typename T>
		TypedResourceHandle<T> create(std::string_view uri, const typename T::DescT& desc) {
			entt::id_type resourceType = entt::type_id<T>().index();
			auto found = _providers.find(resourceType);

			if (found != _providers.end()) {
				auto provider = (TypedResourceProvider<T>*)found->second.get();
				std::unique_ptr<Resource> res = provider->create(uri, desc);

				if (res) {
					std::shared_ptr<ResourceHandleState> state = std::make_shared<ResourceHandleState>();
					state->resource = std::move(res);
					state->uri = std::string(uri);
					state->loaded = true;

					_resources[entt::hashed_string(uri.data())] = ResourceHandle(state);

					return ResourceHandle(state);
				}
			} else {
				spdlog::error("Failed to create resource {}. A resource provider for type {} could not be found", resourceType);
			}

			return TypedResourceHandle<T>();
		}

		template <typename T>
		TypedResourceHandle<T> load(std::string_view uri) {
			entt::id_type resourceType = entt::type_id<T>().index();

			if (_providers.find(resourceType) != _providers.end()) {
				return TypedResourceHandle<T>(load(uri, resourceType));
			} else {
				spdlog::error("Failed to load resource {}. A resource provider for type {} could not be found", entt::type_id<T>().name());
			}

			return TypedResourceHandle<T>();
		}

		template <typename T>
		TypedResourceHandle<T> get(std::string_view uri) {
			UriHash uriHash = entt::hashed_string(uri.data());

			auto found = _resources.find(uriHash);
			if (found != _resources.end()) {
				return TypedResourceHandle<T>(found->second);
			}

			return TypedResourceHandle<T>();
		}

		ResourceHandle get(std::string_view uri) {
			UriHash uriHash = entt::hashed_string(uri.data());

			auto found = _resources.find(uriHash);
			if (found != _resources.end()) {
				return found->second;
			}

			return ResourceHandle();
		}

		void reload(std::string_view uri) {
			ResourceHandle handle = get(uri);
			
			if (handle != nullptr) {
				auto found = _providers.find(handle.getType().index());

				if (found != _providers.end()) {
					std::unique_ptr<Resource> res = found->second->load(uri);

					if (res) {
						handle.getState().resource = std::move(res);
					}
				} else {
					spdlog::error("Failed to reload resource {}. A resource provider for type {} could not be found", handle.getType().name());
				}
			} else {
				spdlog::error("Failed to reload resource {}, the resource has not been loaded", uri);
			}
		}

		void update() {
			for (auto it = _resources.begin(); it != _resources.end(); ++it) {
				if (it->second.useCount() == 1) {
					spdlog::debug("Unloading resource: {}", it->second.getUri());
					it = _resources.erase(it);
				}
			}
		}
	};
}
