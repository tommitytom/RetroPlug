#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <entt/core/type_info.hpp>

#include "Resource.h"

namespace rp {
	struct ResourceHandleState {
		std::unique_ptr<Resource> resource;
		std::string uri; // Store here as well as Resource, since the resource above might be a placeholder with a different URI
		std::atomic<bool> loaded = false;
	};

	class ResourceHandle {
	protected:
		std::shared_ptr<ResourceHandleState> _state;

	public:
		ResourceHandle() = default;
		ResourceHandle(const std::shared_ptr<ResourceHandleState>& state): _state(state) {}
		ResourceHandle(const ResourceHandle& other): _state(other._state) {}
		~ResourceHandle() = default;

		bool operator==(const ResourceHandle& other) const {
			return other._state == _state;
		}

		bool operator!=(const ResourceHandle& other) const {
			return other._state != _state;
		}

		bool operator==(std::nullptr_t) const {
			return _state == nullptr;
		}

		bool isValid() const {
			return _state != nullptr;
		}

		const std::string& getUri() const {
			assert(isValid());
			return _state->uri;
		}

		const entt::type_info& getType() const {
			assert(isValid());
			return _state->resource->getType();
		}
		
		size_t useCount() const noexcept {
			assert(isValid());
			return _state.use_count();
		}
		
		Resource& getResourceBase() {
			assert(isValid());
			return *_state->resource;
		}

		const Resource& getResourceBase() const {
			assert(isValid());
			return *_state->resource;
		}

		ResourceHandleState& getState() {
			assert(isValid());
			return *_state;
		}

		const ResourceHandleState& getState() const {
			assert(isValid());
			return *_state;
		}

		bool isLoaded() const {
			assert(isValid());
			return _state->loaded;
		}
	};

	template <typename T>
	class TypedResourceHandle : public ResourceHandle {
	public:
		TypedResourceHandle() = default;
		TypedResourceHandle(const ResourceHandle& other): ResourceHandle(other) {}
		~TypedResourceHandle() = default;

		T& operator->() {
			return getResource();
		}

		const T& operator->() const {
			return getResource();
		}

		T& getResource() {
			assert(isValid());
			return *((T*)_state->resource.get());
		}

		const T& getResource() const {
			assert(isValid());
			return *((T*)_state->resource.get());
		}
	};
}
