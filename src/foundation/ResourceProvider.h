#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "ResourceHandle.h"

namespace rp {
	class Resource;

	class ResourceProvider {
	public:
		virtual std::shared_ptr<Resource> load(std::string_view uri) = 0;

		virtual void reload(ResourceHandle handle) = 0;
	};

	template <typename T>
	class TypedResourceProvider : public ResourceProvider {
	public:
		virtual std::shared_ptr<Resource> create(const typename T::DescT& desc, std::vector<std::string>& deps) = 0;

		std::shared_ptr<Resource> create(const typename T::DescT& desc) {
			std::vector<std::string> deps;
			return create(desc, deps);
		}

		virtual bool update(T& resource, const typename T::DescT& desc) { return false; }

		virtual void reload(ResourceHandle handle) override {
			ResourceHandleState& state = handle.getState();

			if (state.fromDisk) {
				state.resource = load(state.uri);
			} else {
				assert(state.desc);
				state.resource = create(entt::any_cast<T::DescT>(state.desc));
			}			
		}

		std::shared_ptr<T> createTyped(const typename T::DescT& desc) {
			return std::static_pointer_cast<T>(create(desc));
		}
	};
}
