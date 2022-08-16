#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/core/any.hpp>
#include <entt/core/type_info.hpp>

#include "Resource.h"

namespace rp {
	struct ResourceHandleState {
		std::shared_ptr<Resource> resource;
		std::string uri;
		std::vector<std::string> deps;
		entt::any desc;
		bool loaded = false;
		bool fromDisk;
	};

	template <typename T>
	class TypedResourceHandle;

	class ResourceHandle {
	protected:
		std::shared_ptr<ResourceHandleState> _state;

	public:
		ResourceHandle() = default;
		ResourceHandle(const std::shared_ptr<ResourceHandleState>& state): _state(state) {}
		ResourceHandle(const ResourceHandle& other): _state(other._state) {}
		~ResourceHandle() = default;

		ResourceHandle& operator=(std::nullptr_t) {
			_state.reset();
			return *this;
		}

		bool operator==(const ResourceHandle& other) const {
			return other._state == _state;
		}

		bool operator!=(const ResourceHandle& other) const {
			return other._state != _state;
		}

		/*bool operator==(std::nullptr_t) const {
			return _state == nullptr;
		}*/

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

		template <typename T>
		const T& getResourceAs() const {
			assert(isValid());
			return (const T&)(*_state->resource);
		}

		template <typename T>
		T& getResourceAs() {
			assert(isValid());
			return (T&)(*_state->resource);
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

		const std::vector<std::string> getDependencies() const {
			assert(isValid());
			return _state->deps;
		}

		template <typename T>
		TypedResourceHandle<T>& getResourceHandleAs() {
			return *((TypedResourceHandle<T>*)this);
		}

		template <typename T>
		const TypedResourceHandle<T>& getResourceHandleAs() const {
			return *((const TypedResourceHandle<T>*)this);
		}
	};

	template <typename T>
	class TypedResourceHandle : public ResourceHandle {
	public:
		TypedResourceHandle() = default;
		TypedResourceHandle(const ResourceHandle& other): ResourceHandle(other) {}
		~TypedResourceHandle() = default;

		ResourceHandle& operator=(std::nullptr_t) {
			_state.reset();
			return *this;
		}

		const typename T::DescT& getDesc() const {
			assert(isValid());
			return entt::any_cast<const T::DescT&>(_state->desc);
		}

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

	using ResourceHandleLookup = std::unordered_map<UriHash, ResourceHandle>;
}
