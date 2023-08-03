#include "LuaReflection.h"

#include <assert.h>

namespace fw::LuaReflection {
	std::string internal::splitTypeName(std::string_view name, std::array<std::string, internal::MAX_NAMESPACES>& namespaces) {
		size_t nsCount = 0;
		size_t offset = 0;
		auto foundOffset = name.find_first_of(':');

		while (foundOffset != std::string_view::npos) {
			assert(nsCount < internal::MAX_NAMESPACES);
			namespaces[nsCount++] = std::string(name.substr(offset, foundOffset - offset));
			name = name.substr(foundOffset + 2);
			foundOffset = name.find_first_of(':');
		}

		return std::string(name);
	}

	std::string internal::splitTypeName(std::string_view name, std::vector<std::string>& namespaces) {
		size_t nsCount = 0;
		size_t offset = 0;
		auto foundOffset = name.find_first_of(':');

		while (foundOffset != std::string_view::npos) {
			assert(nsCount < internal::MAX_NAMESPACES);
			namespaces.push_back(std::string(name.substr(offset, foundOffset - offset)));
			name = name.substr(foundOffset + 2);
			foundOffset = name.find_first_of(':');
		}

		return std::string(name);
	}
}
