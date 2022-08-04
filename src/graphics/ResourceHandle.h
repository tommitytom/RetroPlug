#pragma once

#include <memory>

namespace rp {
	template <typename T>
	struct ResourceHandleState {
		std::atomic<bool> loaded = false;
		std::unique_ptr<T> resource;
	};

	class ResourceProvider {

	};

	class StreamingResourceProvider {

	};

	template <typename T>
	class ResourceHandle {
	private:
		std::shared_ptr<ResourceHandleState> _state;

	public:
		ResourceHandle() {}
		ResourceHandle(std::shared_ptr<std::unique_ptr<T>>&& resource) : _resource(std::move(resource)) {}

		bool isValid() const {
			return _resource || _resource->get();
		}

		T* getResource() {
			if (_resource) {
				return _resource->get();
			}

			return nullptr;
		}
	};
}
