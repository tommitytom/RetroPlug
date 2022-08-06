#pragma once

#include <string>
#include <entt/core/type_info.hpp>

namespace rp {
	using UriHash = entt::id_type;

	class Resource {
	private:
		std::string _uri;
		entt::type_info _type;

	public:
		Resource(entt::type_info type): _type(type) {}
		~Resource() = default;

		const entt::type_info& getType() const {
			return _type;
		}

		const std::string& getUri() const {
			return _uri;
		}
	};
}
