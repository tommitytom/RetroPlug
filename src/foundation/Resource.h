#pragma once

#include <string_view>
#include <entt/core/type_info.hpp>

namespace rp {
	using UriHash = entt::id_type;

	namespace ResourceUtil {
		static UriHash hashUri(std::string_view uri) {
			return entt::hashed_string(uri.data(), uri.size());
		}
	}

	class Resource {
	private:
		entt::type_info _type;

	public:
		Resource(const entt::type_info& type): _type(type) {}
		~Resource() = default;

		const entt::type_info& getType() const {
			return _type;
		}
	};
}
