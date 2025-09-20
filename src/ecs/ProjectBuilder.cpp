#include "ProjectBuilder.h"

#include "foundation/FsUtil.h"
#include "ecs/EcsProjectSerializer.h"
#include "ecs/RetroPlugProjectContext.h"

namespace rp {
	namespace fs = std::filesystem;

	enum class FileSystemFileType {
		Unknown,
		File,
		Directory,
		Archive
	};

	struct FileSystemEntry {
		std::string name;
		fs::path path;
		FileSystemFileType type = FileSystemFileType::Unknown;
		size_t size = 0;
	};

	class FileSystemArchive;

	class FileSystemArchive {
	public:
		FileSystemArchive() {}
		virtual ~FileSystemArchive() = 0;

		virtual bool list() { return true; }
	};

	struct ParsedPath {
		FileSystemFileType type = FileSystemFileType::Unknown;
		fs::path fsPath; // Path on filesystem
		fs::path archivePath; // Path within archive
	};

	class FileSystemArchiveFactory {
	private:
		std::vector<fs::path> _extensions;

	public:
		FileSystemArchiveFactory(std::vector<fs::path>&& extensions) : _extensions(std::move(extensions)) {}
		virtual ~FileSystemArchiveFactory() = 0;

		virtual bool canOpen(const fs::path& path) const {
			return std::find(_extensions.begin(), _extensions.end(), path.extension()) != _extensions.end();
		}

		virtual std::unique_ptr<FileSystemArchive> open(const fs::path& path) const = 0;

		const std::vector<fs::path>& getExtensions() const {
			return _extensions;
		}
	};

	class FileSystem {
	private:
		std::vector<std::unique_ptr<FileSystemArchive>> _archiveFactories;
		std::unordered_map<fs::path, FileSystemArchive> _openArchives;

	public:
		void listPath(const fs::path& path) {

		}
	};


	class SavArchive : public FileSystemArchive {
	};

	class SavArchiveFactory : public FileSystemArchiveFactory {
	public:
		SavArchiveFactory() : FileSystemArchiveFactory({ ".sav" }) {}
		~SavArchiveFactory() = default;

		std::unique_ptr<FileSystemArchive> open(const fs::path& path) const override {
			return std::make_unique<SavArchive>();
		}
	};

	bool ensureMountPath(const fs::path& mountPath, const fs::path& path) {
		return mountPath.empty() || path.string().starts_with(mountPath.string());
	}

	bool resolveEntries(SystemLoadComponent& load, const fs::path& rootPath) {
		bool error = false;

		for (auto& [type, entry] : load.entries) {
			if (entry.data().empty()) {
				fs::path path(entry.path);

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
		for (const NamedEntry& entry : entries) {
			if (entry.type == type) {
				count++;
			}
		}

		return count;
	}

	fs::path getEntryPath(const NamedEntryVector& entries, const std::string& type) {
		for (const NamedEntry& entry : entries) {
			if (entry.type == type) {
				return entry.path;
			}
		}

		return "";
	}

	const NamedEntry* findEntry(const NamedEntryVector& entries, const std::string& type) {
		for (const NamedEntry& entry : entries) {
			if (entry.type == type) {
				return &entry;
			}
		}

		return nullptr;
	}

	ProjectPathContext& getPathContext(entt::registry& registry) {
		return registry.ctx().at< ProjectPathContext>();
	}

	HooksContext& getHooksContext(entt::registry& registry) {
		return registry.ctx().at< HooksContext>();
	}

	bool ProjectBuilder::loadFromFile(entt::registry& registry, fs::path path) {
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

	fs::path getAdjacentProject(fs::path path) {
		return path.replace_extension(".rplg");
		if (fs::exists(path)) {
			return path;
		}

		return "";
	}

	bool ProjectBuilder::loadFromPaths(entt::registry& registry, PathVector paths) {
		ProjectPathContext& pathCtx = getPathContext(registry);
		const HooksContext& hooksCtx = getHooksContext(registry);
		//const RetroPlugProjectContext& ctx = getContext(registry);

		/*spdlog::info("Loading project from the following path{}:", paths.size() > 1 ? "s" : "");
		for (const auto& path : paths) {
			if (ensureMountPath(pathCtx.mountPath, path)) {
				spdlog::info(" - {}", path.string());
			} else {
				spdlog::error(" - {} (outside mount path {})", path.string(), pathCtx.mountPath.string());
				return false;
			}
		}*/

		// Remove non existing paths
		paths.erase(std::remove_if(paths.begin(), paths.end(), [](const fs::path& path) {
			if (!fs::exists(path)) {
				spdlog::warn("Path does not exist: {}", path.string());
				return true;
			}
			return false;
		}), paths.end());

		if (paths.empty()) {
			spdlog::error("Unable to load: No valid paths");
			return false;
		}

		pathCtx.projectPath.clear();
		pathCtx.projectRoot.clear();

		const int32 projIndex = indexOfExtension(paths, ".rplg");
		if (projIndex != -1) {
			// Just load the project
			return loadFromFile(registry, paths[0]);
		}

	const int32 stateIndex = indexOfExtension(paths, ".state");
		const int32 savIndex = indexOfExtension(paths, ".sav");
		int32 romIndex = indexOfExtension(paths, ".gb");
		if (romIndex == -1) romIndex = indexOfExtension(paths, ".gbc");

		SystemLoadComponent load;

		if (stateIndex != -1) {
			fs::path projPath = getAdjacentProject(paths[stateIndex]);
			if (fs::exists(projPath)) return loadFromFile(registry, projPath);
			load.entries["state"] = { .path = paths[stateIndex].string() };
			pathCtx.projectPath = projPath;
		} else if (savIndex != -1) {
			fs::path projPath = getAdjacentProject(paths[savIndex]);
			if (fs::exists(projPath)) return loadFromFile(registry, projPath);
			load.entries["sram"] = { .path = paths[savIndex].string() };
			pathCtx.projectPath = projPath;
		}

		if (romIndex == -1) {
			spdlog::error("Unable to load: No ROM provided");
			return false;
		}

		load.entries["rom"] = { .path = paths[romIndex].string() };

		entt::entity system = registry.create();
		bool valid = ProjectBuilder::addSystemWithConfig(registry, system, std::forward<SystemLoadComponent>(load), SameBoyComponent{});
		if (!valid) {
			return false;
		}

		if (!pathCtx.projectPath.empty()) {
			saveToFile(registry, pathCtx.projectPath);
		}

		return true;

/*
		if (stateIndex != -1) {
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

		// Only support single rom/sram for now
		if (romCount > 1 || sramCount > 1) {
			spdlog::warn("Unable to load: Multiple ROMs or SRAMs provided");
			return false;
		}

		SystemLoadComponent load;

		const NamedEntry* romEntry = findEntry(entries, "rom");
		const NamedEntry* sramEntry = findEntry(entries, "sram");

		if (!romEntry && sramEntry) {
			if (!sramEntry->path.empty()) {
				auto projectPath = fs::path(sramEntry->path).replace_extension(".rplg");
				if (fs::exists(projectPath)) return loadFromFile(registry, projectPath);

				auto romPath = fs::path(sramEntry->path).replace_extension(".gb");
				if (fs::exists(romPath)) {
					load.entries["rom"] = { .path = romPath.string() };
				}
			}

			load.entries["sram"] = { .path = sramEntry->path.string(), .data = sramEntry->data };
		} else if (romEntry && !sramEntry) {
			if (!romEntry->path.empty()) {
				auto projectPath = fs::path(romEntry->path).replace_extension(".rplg");
				if (fs::exists(projectPath)) return loadFromFile(registry, projectPath);

				auto sramPath = fs::path(romEntry->path).replace_extension(".sav");
				if (fs::exists(sramPath)) {
					load.entries["sram"] = { .path = sramPath.string() };
				}
			}

			load.entries["rom"] = { .path = romEntry->path.string(), .data = romEntry->data };
		} else if (romEntry && sramEntry) {
			load.entries["sram"] = { .path = sramEntry->path.string(), .data = sramEntry->data };
			load.entries["rom"] = { .path = romEntry->path.string(), .data = romEntry->data };
		}

		if (romCount == 1) {
			spdlog::info("Creating new project with the following entries:");
			for (const auto& [type, entry] : load.entries) {
				spdlog::info(" - {}: {}", type, entry.path.empty() ? "[data]" : entry.path);
			}

			// Make project path relative to sav if possible

			if (load.entries.contains("sram") && !load.entries["sram"].path.empty()) {
				pathCtx.projectPath = load.entries["sram"].path;
			} else {
				pathCtx.projectPath = "";
			}

			if (!pathCtx.projectPath.empty()) {
				pathCtx.projectPath.replace_extension(".rplg");
				pathCtx.projectRoot = pathCtx.projectPath.parent_path();
			} else {
				pathCtx.projectRoot = "/";
			}

			entt::entity system = registry.create();
			ProjectBuilder::addSystemWithConfig(registry, system, std::forward<SystemLoadComponent>(load), SameBoyComponent{});

			if (!pathCtx.projectPath.empty()) {
				//saveToFile(registry, pathCtx.projectPath);
			} else {
				spdlog::warn("No project path could be determined, not saving project");
			}

			return true;
		}

		return false;*/
	}

	bool ProjectBuilder::handleLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, entt::id_type systemType) {
		const HooksContext& ctx = getHooksContext(registry);
		const ProjectPathContext& pathCtx = getPathContext(registry);
		const ErrorComponent* error = nullptr;

		resolveEntries(load, pathCtx.projectRoot);

		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onBeforeLoad(registry, entity, load); });
		error = registry.try_get<ErrorComponent>(entity);
		if (error) {
			spdlog::error("Failed onBeforeLoad hooks for entity {}: {}", (uint32)entity, error->error);
			return false;
		}

		eachHook(systemType, ctx.systemHooks, [&](const SystemHookBase& hook) { hook.onLoad(registry, entity, load); });
		error = registry.try_get<ErrorComponent>(entity);
		if (error) {
			spdlog::error("Failed onLoad hooks for entity {}", (uint32)entity, error->error);
			return false;
		}

		eachHook(systemType, ctx.serviceHooks, [&](const SystemHookBase& hook) { hook.onAfterLoad(registry, entity, load); });
		error = registry.try_get<ErrorComponent>(entity);
		if (error) {
			spdlog::error("Failed onAfterLoad hooks for entity {}", (uint32)entity, error->error);
			return false;
		}

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

	bool ProjectBuilder::saveToFile(entt::registry& registry, fs::path path) {
		ProjectPathContext& pathCtx = getPathContext(registry);

		if (path.empty()) {
			if (pathCtx.projectPath.empty()) {
				spdlog::error("No project path set");
				return false;
			}

			path = pathCtx.projectPath;
		}

		if (!ensureMountPath(pathCtx.mountPath, path)) {
			spdlog::error("Path {} is not within mount path {}", path.string(), pathCtx.mountPath.string());
			return false;
		}

		pathCtx.projectPath = path;
		pathCtx.projectRoot = pathCtx.projectPath.parent_path();

		// Ensure all entries are relative to the new project root!
		for (const auto& [e, c] : registry.view<SystemLoadComponent>().each()) {
			for (auto& [k, v] : c.entries) {
				fs::path entryPath(v.path);
				if (entryPath.is_relative()) {
					v.path = (pathCtx.projectRoot / entryPath).lexically_normal().string();
				}
			}

			auto foundSram = c.entries.find("sram");
			if (foundSram != c.entries.end()) {
				fs::path sramPath = path;
				foundSram->second.path = sramPath.replace_extension(".sav").string();
			} else {
				auto foundState = c.entries.find("state");
				if (foundState != c.entries.end()) {
					fs::path statePath = path;
					foundState->second.path = statePath.replace_extension(".state").string();
				} else {
					fs::path statePath = path;
					c.entries["state"] = { .path = statePath.replace_extension(".state").string() };
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

		spdlog::info("Project saved to file: {}", path.string());

		for (const auto& [e, load, state] : registry.view<SystemLoadComponent, SystemStateComponent>().each()) {
			if (state.saveType == SaveType::Sram) {
				auto found = load.entries.find("sram");
				if (found != load.entries.end() && !found->second.path.empty()) {
					const VersionedMemory* sram = state.find(MemoryType::Sram);
					if (sram) {
						if (!fw::FsUtil::writeFile(found->second.path, sram->data)) {
							spdlog::error("Failed to write SRAM file: {}", found->second.path);
						} else {
							spdlog::info("Saved SRAM to file: {}", found->second.path);
						}
					} else {
						spdlog::warn("Not saving SRAM file, no SRAM data present");
					}
				} else {
					spdlog::warn("Not saving SRAM file, no SRAM path present");
				}
			} else {
				if (state.state.size()) {
					fs::path statePath = path.replace_extension(".state");
					if (!fw::FsUtil::writeFile(statePath, state.state)) {
						spdlog::error("Failed to write state file: {}", statePath.string());
					} else {
						spdlog::info("Saved state to file: {}", statePath.string());
					}
				} else {
					spdlog::warn("Not saving state file, no state data present");
				}
			}
		}

		return true;
	}

	bool ProjectBuilder::deserializeJson(entt::registry& registry, std::string_view str, const fs::path& rootPath) {
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
