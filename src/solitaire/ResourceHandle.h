#pragma once

namespace rp {
	template <typename T>
	class ResourceHandle {
	private:
		std::shared_ptr<std::unique_ptr<T>> _resource;

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
