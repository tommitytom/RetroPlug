#pragma once

struct yyjson_val;
struct yyjson_mut_doc;
struct yyjson_mut_val;

namespace rp {
	struct ProjectSerializerContext {
		yyjson_mut_doc* doc;
		yyjson_mut_val* componentArray;
	};

	struct ProjectDeserializerContext {
		yyjson_val* componentData;
	};

	using EmplacerFunc = std::function<void(entt::registry&, entt::entity, ProjectDeserializerContext&)>;

	template <typename Component>
	void emplacerFunc(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) {

	}
}
