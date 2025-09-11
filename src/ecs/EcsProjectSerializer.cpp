#include "EcsProjectSerializer.h"

#include "core/CoreComponents.h"
#include "ecs/RetroPlugProjectContext.h"
#include "foundation/Replicator.h"

namespace rp {
	void ProjectSerializer::serialize(const entt::registry& registry, std::string& target) {
		const HooksContext& hooksCtx = registry.ctx().at<HooksContext>();

		yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
		yyjson_mut_val* root = yyjson_mut_obj(doc);
		yyjson_mut_doc_set_root(doc, root);

		yyjson_mut_val* systems = yyjson_mut_arr(doc);
		yyjson_mut_obj_add_val(doc, root, "systems", systems);

		for (const entt::entity entity : registry.view<SystemComponent>()) {
			yyjson_mut_val* system = yyjson_mut_obj(doc);
			yyjson_mut_arr_append(systems, system);

			yyjson_mut_val* components = yyjson_mut_arr(doc);
			yyjson_mut_obj_add_val(doc, system, "components", components);

			ProjectSerializerContext ctx{ doc, components };

			serializeComponent<SystemLoadComponent>(registry, entity, ctx);

			eachHook(hooksCtx.systemHooks, [&](const SystemHookBase& hook) { hook.onSerialize(registry, entity, ctx); });
			eachHook(hooksCtx.serviceHooks, [&](const SystemHookBase& hook) { hook.onSerialize(registry, entity, ctx); });
		};

		const ProjectConfig& projectConfig = registry.ctx().at<ProjectConfig>();
		yyjson_mut_val* config = yyjson_mut_obj(doc);
		JsonUtil::write(projectConfig, doc, config);
		yyjson_mut_obj_add_val(doc, root, "config", config);

		yyjson_write_err err;
		yyjson_write_flag _flag = rfl::json::pretty;
		const char* json_c_str = yyjson_mut_write_opts(doc, _flag, NULL, NULL, &err);
		if (!json_c_str) {
			throw std::runtime_error("An error occured while writing to JSON: " +
				std::string(err.msg));
		}

		target = std::string(json_c_str);
		free((void*)json_c_str);
	}

	bool ProjectSerializer::deserialize(entt::registry& registry, std::string_view source) {
		const HooksContext& hooksCtx = registry.ctx().at<HooksContext>();

		yyjson_doc* doc = yyjson_read(source.data(), source.size(), 0);
		yyjson_val* root = yyjson_doc_get_root(doc);

		yyjson_val* config = yyjson_obj_get(root, "config");
		registry.ctx().at<ProjectConfig>() = JsonUtil::read<ProjectConfig>(config).value_or(ProjectConfig());

		yyjson_val* systems = yyjson_obj_get(root, "systems");

		size_t systemIdx, systemMax;
		yyjson_val* system;
		yyjson_arr_foreach(systems, systemIdx, systemMax, system) {
			entt::entity entity = fw::Replicator::spawn(registry);

			yyjson_val* components = yyjson_obj_get(system, "components");

			size_t componentIdx, componentMax;
			yyjson_val* component;
			yyjson_arr_foreach(components, componentIdx, componentMax, component) {
				ProjectDeserializerContext ctx{ component };

				ProjectSerializer::deserializeComponent<SystemLoadComponent>(registry, entity, ctx);

				eachHook(hooksCtx.systemHooks, [&](const SystemHookBase& hook) { hook.onDeserialize(registry, entity, ctx); });
				eachHook(hooksCtx.serviceHooks, [&](const SystemHookBase& hook) { hook.onDeserialize(registry, entity, ctx); });
			}
		}

		return true;
	}
}
