#pragma once

#include <filesystem>
#include <unordered_map>

#include <entt/fwd.hpp>
#include <spdlog/spdlog.h>

#include "ResourceHandle.h"
#include "ResourceProvider.h"

namespace rp {
	class ResourceManager {
	private:
		std::unordered_map<entt::id_type, std::shared_ptr<ResourceProvider>> _providers;
		ResourceHandleLookup _resources;
		std::filesystem::path _rootPath;

	public:
		ResourceManager() = default;
		~ResourceManager() = default;

		void setRootPath(const std::filesystem::path& path) {
			_rootPath = (std::filesystem::current_path() / path).make_preferred().lexically_normal();
		}

		const std::filesystem::path& getRootPath() const {
			return _rootPath;
		}

		template <typename ResourceT, typename ProviderT>
		void addProvider(std::unique_ptr<ProviderT>&& provider) {
			_providers[entt::type_id<ResourceT>().index()] = std::move(provider);
		}
		
		template <typename ResourceT, typename ProviderT>
		void addProvider() {
			addProvider<ResourceT, ProviderT>(std::make_unique<ProviderT>());
		}

		const ResourceHandleLookup& getLookup() const {
			return _resources;
		}

		template <typename ResourceT>
		ResourceProvider& getProvider() {
			return *_providers[entt::type_id<ResourceT>().index()];
		}

		ResourceHandle load(std::string_view uri, entt::id_type resourceType) {
			auto found = _providers.find(resourceType);

			if (found != _providers.end()) {
				std::shared_ptr<Resource> res = found->second->load(uri);

				if (res) {
					std::shared_ptr<ResourceHandleState> state = std::make_shared<ResourceHandleState>();
					state->resource = std::move(res);
					state->uri = std::string(uri);
					state->loaded = true;
					state->fromDisk = true;

					_resources[entt::hashed_string(uri.data())] = ResourceHandle(state);

					return ResourceHandle(state);
				}
			} else {
				spdlog::error("Failed to load resource {}. A resource provider for type {} could not be found", resourceType);
			}

			return ResourceHandle();
		}

		template <typename T>
		bool update(TypedResourceHandle<T> handle, const typename T::DescT& desc) {
			assert(!handle.getState().fromDisk);

			TypedResourceProvider<T>* provider = findProvider<T>();

			if (provider) {
				return provider->update(handle.getResource(), desc);
			}
			
			spdlog::error("Failed to update anonymous resource. A resource provider for type {} could not be found", entt::type_id<T>().name());

			return false;
		}

		template <typename T>
		TypedResourceHandle<T> create(const typename T::DescT& desc) {
			return create<T>("", desc);
		}

		template <typename T>
		TypedResourceHandle<T> create(std::string_view uri, const typename T::DescT& desc) {
			TypedResourceProvider<T>* provider = findProvider<T>();

			if (provider) {
				std::vector<std::string> deps;
				std::shared_ptr<Resource> res = provider->create(desc, deps);

				if (res) {
					std::shared_ptr<ResourceHandleState> state = std::make_shared<ResourceHandleState>();
					state->resource = std::move(res);
					state->uri = std::string(uri);
					state->deps = std::move(deps);
					state->desc = std::move(desc);
					state->loaded = true;
					state->fromDisk = false;

					ResourceHandle handle = ResourceHandle(state);

					if (uri.size()) {
						_resources[entt::hashed_string(uri.data())] = handle;
					}

					return handle;
				}
			} else {
				spdlog::error("Failed to create resource {}. A resource provider for type {} could not be found", uri.size() ? uri : "<unknown>", entt::type_id<T>().name());
			}

			return TypedResourceHandle<T>();
		}

		template <typename T>
		TypedResourceHandle<T> load(std::string_view uri) {
			entt::id_type resourceType = entt::type_id<T>().index();

			if (_providers.find(resourceType) != _providers.end()) {
				return TypedResourceHandle<T>(load(uri, resourceType));
			} else {
				spdlog::error("Failed to load resource {}. A resource provider for type {} could not be found", uri, entt::type_id<T>().name());
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
			
			if (handle.isValid()) {
				UriHash uriHash = ResourceUtil::hashUri(uri);
				auto found = _providers.find(handle.getType().index());

				if (found != _providers.end()) {
					found->second->reload(handle);

					for (const auto& [k, v] : _resources) {
						for (const std::string& dep : v.getDependencies()) {
							if (uriHash == ResourceUtil::hashUri(dep)) {
								reload(v.getUri());
							}
						}
					}
				} else {
					spdlog::error("Failed to reload resource {}. A resource provider for type {} could not be found", handle.getType().name());
				}
			} else {
				spdlog::error("Failed to reload resource {}, the resource has not been loaded", uri);
			}
		}

		void frame() {
			for (auto it = _resources.begin(); it != _resources.end(); ++it) {
				if (it->second.useCount() == 1) {
					spdlog::debug("Unloading resource: {}", it->second.getUri());
					it = _resources.erase(it);
				}
			}
		}

		private:
			template <typename T>
			TypedResourceProvider<T>* findProvider() {
				entt::id_type resourceType = entt::type_id<T>().index();
				auto found = _providers.find(resourceType);

				if (found != _providers.end()) {
					return (TypedResourceProvider<T>*)found->second.get();
				}

				return nullptr;
			}
	};
}
