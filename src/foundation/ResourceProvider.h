#pragma once

#include <memory>
#include <string_view>

namespace rp {
	class Resource;

	class ResourceProvider {
	public:
		virtual std::unique_ptr<Resource> load(std::string_view uri) = 0;
	};

	template <typename T>
	class TypedResourceProvider : public ResourceProvider {
	public:
		virtual std::unique_ptr<Resource> create(std::string_view uri, const typename T::DescT& desc) = 0;
	};
}
