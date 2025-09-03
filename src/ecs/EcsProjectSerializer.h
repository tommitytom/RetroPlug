#pragma once

#include <entt/entity/registry.hpp>

#include "ecs/JsonUtil.h"
#include "ecs/RetroPlugProjectContext.h"
#include "ecs/ProjectSerializerContext.h"

namespace rp::ProjectSerializer {
	template <typename T>
	std::string_view getTypeName() {
		std::string_view name = entt::type_id<T>().name();

		if (name.starts_with("class ") || name.starts_with("struct ")) {
			size_t offset = name.find_first_of(' ');

			if (offset != std::string::npos) {
				return name.substr(offset + 1);
			}
		}

		return name;
	}

	template <typename Component>
	void serializeComponent(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) {
		const Component* comp = registry.try_get<Component>(entity);
		if (comp) {
			std::string_view typeName = getTypeName<Component>();
			yyjson_mut_val* compObj = yyjson_mut_obj(ctx.doc);
			yyjson_mut_obj_add_uint(ctx.doc, compObj, "type", entt::type_hash<Component>::value());
			yyjson_mut_obj_add_strn(ctx.doc, compObj, "name", typeName.data(), typeName.size());

			yyjson_mut_val* data = yyjson_mut_obj(ctx.doc);
			JsonUtil::write(*comp, ctx.doc, data);
			yyjson_mut_obj_add_val(ctx.doc, compObj, "data", data);
			yyjson_mut_arr_append(ctx.componentArray, compObj);
		}
	}

	template <typename Component>
	bool deserializeComponent(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) {
		const entt::id_type type = (entt::id_type)yyjson_get_uint(yyjson_obj_get(ctx.componentData, "type"));
		if (entt::type_hash<Component>::value() == type) {
			yyjson_val* data = yyjson_obj_get(ctx.componentData, "data");
			Component comp;
			JsonUtil::read(comp, data);
			registry.emplace_or_replace<Component>(entity, std::move(comp));
			return true;
		}

		return false;
	}

	namespace {
		// Helper to unpack type_list (internal implementation detail)
		template <typename... Components>
		void serializeComponentList(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx, entt::type_list<Components...>) {
			(serializeComponent<Components>(registry, entity, ctx), ...);
		}
	}

	template <typename TypeList> requires std::is_same_v<TypeList, typename TypeList::type>  // Check if it's a type_list
	void serializeComponents(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) {
		serializeComponentList(registry, entity, ctx, TypeList{});
	}

	void serialize(const entt::registry& registry, std::string& target);

	void deserialize(entt::registry& registry, std::string_view source);
}
