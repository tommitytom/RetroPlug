#include "ProjectBuilder.h"

#include "foundation/FsUtil.h"
#include "ecs/EcsProjectSerializer.h"
#include "ecs/RetroPlugProjectContext.h"

namespace rp {
	bool ensureMountPath(const std::filesystem::path& mountPath, const std::filesystem::path& path) {
		return mountPath.empty() || path.string().starts_with(mountPath.string());
	}

	bool resolveEntries(SystemLoadComponent& load, const std::filesystem::path& rootPath) {
		bool error = false;

		for (auto& [type, entry] : load.entries) {
			if (entry.data().empty()) {
				std::filesystem::path path(entry.path);

				if (!path.is_absolute()) {
					path = (rootPath / path).lexically_normal();
				} else {
					path = path.lexically_normal();
				}

				if (!fw::FsUtil::readFile(path.string(), entry.data())) {
					error = true;
					spdlog::error("Failed to read file: {}", path.string());
				}
			}
		}

		return error;
	}

	int32 indexOfExtension(const PathVector& paths, const std::string& ext) {
		for (size_t i = 0; i < paths.size(); ++i) {
			if (paths[i].extension() == ext) {
				return (int32)i;
			}
		}
		return -1;
	}

	size_t countEntries(const NamedEntryVector& entries, const std::string& type) {
		size_t count = 0;
		for (const auto& [t, p] : entries) {
			if (t == type) {
				count++;
			}
		}

		return count;
	}

	std::filesystem::path getEntryPath(const NamedEntryVector& entries, const std::string& type) {
		for (const auto& [t, p] : entries) {
			if (t == type) {
				return p;
			}
		}

		return "";
	}

	ProjectPathContext& getPathContext(entt::registry& registry) {
		return registry.ctx().at< ProjectPathContext>();
	}

	HooksContext& getHooksContext(entt::registry& registry) {
		return registry.ctx().at< HooksContext>();
	}

	bool ProjectBuilder::loadFromFile(entt::registry& registry, std::filesystem::path path) {
		ProjectPathContext& pathCtx = getPathContext(registry);

		spdlog::info("Loading project from file: {}", path.string());

		if (!ensureMountPath(pathCtx.mountPath, path)) {
			spdlog::error("Path {} is not within mount path {}", path.string(), pathCtx.mountPath.string());
			return false;
		}

		std::string data = fw::FsUtil::readTextFile(path);
		if (data.empty()) {
			spdlog::error("Failed to read project file: {}", path.string());
			return false;
		}

		pathCtx.projectPath = path;

		return deserializeJson(registry, data, pathCtx.projectPath.parent_path());
	}

	bool ProjectBuilder::loadFromPaths(entt::registry& registry, PathVector paths) {
		ProjectPathContext& pathCtx = getPathContext(registry);
		const HooksContext& hooksCtx = getHooksContext(registry);
		//const RetroPlugProjectContext& ctx = getContext(registry);

		spdlog::info("Loading project from the following path{}:", paths.size() > 1 ? "s" : "");
		for (const auto& path : paths) {
			if (ensureMountPath(pathCtx.mountPath, path)) {
				spdlog::info(" - {}", path.string());
			} else {
				spdlog::error(" - {} (outside mount path {})", path.string(), pathCtx.mountPath.string());
				return false;
			}
		}

		// Remove non existing paths
		paths.erase(std::remove_if(paths.begin(), paths.end(), [](const std::filesystem::path& path) {
			if (!std::filesystem::exists(path)) {
				spdlog::warn("Path does not exist: {}", path.string());
				return true;
			}
			return false;
		}), paths.end());

		if (paths.empty()) {
			spdlog::error("Unable to load: No valid paths");
			return false;
		}

		const int32 projIndex = indexOfExtension(paths, ".rplg");
		if (projIndex != -1) {
			// Just load the project
			return loadFromFile(registry, paths[0]);
		}

		NamedEntryVector entries;

		eachHook(hooksCtx.systemHooks, [&](const SystemHookBase& hook) { hook.onFilterEntries(registry, paths, entries); });
		eachHook(hooksCtx.serviceHooks, [&](const SystemHookBase& hook) { hook.onFilterEntries(registry, paths, entries); });

		if (entries.empty()) {
			spdlog::error("Unable to load: Unrecognised path{}:", paths.size() > 1 ? "s" : "");
			return false;
		}

		const size_t romCount = countEntries(entries, "rom");
		const size_t sramCount = countEntries(entries, "sram");

		SystemLoadComponent load;

		if (romCount == 0 && sramCount == 1) {
			auto sramPath = getEntryPath(entries, "sram");

			auto projectPath = std::filesystem::path(sramPath).replace_extension(".rplg");
			if (std::filesystem::exists(projectPath)) {
				return loadFromFile(registry, projectPath);
			}

			auto romPath = std::filesystem::path(sramPath).replace_extension(".gb");
			if (std::filesystem::exists(romPath)) {
				load.entries["sram"] = { .path = sramPath.string() };
				load.entries["rom"] = { .path = romPath.string() };
			}
		} else if (romCount == 1 && sramCount == 0) {
			auto romPath = getEntryPath(entries, "rom");

			auto projectPath = std::filesystem::path(romPath).replace_extension(".rplg");
			if (std::filesystem::exists(projectPath)) {
				return loadFromFile(registry, projectPath);
			}

			auto sramPath = std::filesystem::path(romPath).replace_extension(".sav");
			if (std::filesystem::exists(romPath)) {
				load.entries["sram"] = { .path = sramPath.string() };
				load.entries["rom"] = { .path = romPath.string() };
			}
		}

		spdlog::info("Creating new project with the following entries:");
		for (const auto& [type, entry] : load.entries) {
			spdlog::info(" - {}: {}", type, entry.path);
		}

		if ((romCount == 1 && sramCount == 1) || load.entries.size() == 2) {
			// Make project path relative to sav
			pathCtx.projectPath = load.entries["sram"].path;
			pathCtx.projectPath.replace_extension(".rplg");
			pathCtx.projectRoot = pathCtx.projectPath.parent_path();

			entt::entity system = registry.create();
			ProjectBuilder::addSystemWithConfig(registry, system, std::forward<SystemLoadComponent>(load), SameBoyComponent{});

			if (system != entt::null) {
				//saveToFile(_projectPath);
			}
		}

		return false;
	}

	bool ProjectBuilder::handleLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, entt::id_type systemType) {
		const HooksContext& ctx = getHooksContext(registry);
		const ProjectPathContext& pathCtx = getPathContext(registry);

		resolveEntries(load, pathCtx.projectRoot);
		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onBeforeLoad(registry, entity, load); });
		eachHook(systemType, ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onLoad(registry, entity, load); });
		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onAfterLoad(registry, entity, load); });

		return true;
	}

	bool ProjectBuilder::addSystem(entt::registry& registry, entt::entity entity, SystemLoadComponent&& config, entt::id_type systemType) {
		if (entity == entt::null) {
			entity = registry.create();
		} else if (!registry.valid(entity)) {
			spdlog::error("Attempted to add system to invalid entity {}", (uint32)entity);
			return false;
		}

		// TODO: Support other system types
		registry.emplace<SameBoyComponent>(entity);
		registry.emplace<SystemComponent>(entity, systemType);
		SystemLoadComponent& load = registry.emplace<SystemLoadComponent>(entity, std::move(config));
		return handleLoad(registry, entity, load, systemType);
	}

	bool ProjectBuilder::saveToFile(entt::registry& registry, std::filesystem::path path) {
		ProjectPathContext& pathCtx = getPathContext(registry);

		if (!ensureMountPath(pathCtx.mountPath, path)) {
			spdlog::error("Path {} is not within mount path {}", path.string(), pathCtx.mountPath.string());
			return false;
		}

		pathCtx.projectPath = path;
		pathCtx.projectRoot = pathCtx.projectPath.parent_path();

		// Ensure all entries are relative to the new project root!
		for (const auto& [e, c] : registry.view<SystemLoadComponent>().each()) {
			for (auto& [k, v] : c.entries) {
				std::filesystem::path entryPath(v.path);
				if (entryPath.is_relative()) {
					v.path = (pathCtx.projectRoot / entryPath).lexically_normal().string();
				}
			}
		}

		std::string target;
		ProjectSerializer::serialize(registry, target);

		if (target.empty()) {
			spdlog::error("Failed to serialize project");
			return false;
		}
		if (!fw::FsUtil::writeTextFile(path, target)) {
			spdlog::error("Failed to write project file: {}", path.string());
			return false;
		}
		return true;
	}

	bool ProjectBuilder::deserializeJson(entt::registry& registry, std::string_view str, const std::filesystem::path& rootPath) {
		ProjectPathContext& pathCtx = getPathContext(registry);

		pathCtx.projectRoot = rootPath;

		if (!ProjectSerializer::deserialize(registry, str)) {
			return false;
		}

		for (const auto& [e, system, load] : registry.view<SystemComponent, SystemLoadComponent>().each()) {
			handleLoad(registry, e, load, system.systemType);
		}

		spdlog::info("Project loaded");

		return true;
	}
}
