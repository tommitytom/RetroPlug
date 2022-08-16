#pragma once

#include <FileWatcher/FileWatcher.h>

#include "foundation/ResourceManager.h"
#include "foundation/ProcessUtil.h"

namespace rp {
	class AssetWatcherListener final : public FW::FileWatchListener {
	private:
		ResourceManager* _resourceManager = nullptr;

	public:
		void setResourceManager(ResourceManager* resourceManager) {
			_resourceManager = resourceManager;
		}

		void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) override {
			std::filesystem::path path = std::filesystem::path(dir) / std::filesystem::path(filename);
			std::filesystem::path outputPath = _resourceManager->getRootPath() / filename;

			std::string shaderType;

			if (path.extension() == ".fsc") {
				shaderType = "fragment";
			} else if (path.extension() == ".vsc") {
				shaderType = "vertex";
			}

			if (shaderType.size()) {
				spdlog::info("Compiling {} to {}", filename, outputPath.string());

				int32 ret = ProcessUtil::runProcess("shaderc.exe", {
					"-f", path.string(),
					"-o", outputPath.string(),
					"--platform", "windows",
					"--varyingdef", (std::filesystem::path(dir) / "varying.def.sc").string(),
					"--type", shaderType,
					"-p", "150"
				});

				if (ret == 0) {
					_resourceManager->reload(outputPath.string());
				} else {
					spdlog::error("Failed to run");
				}
			}
		}
	};

	class AssetWatcher {
	private:
		ResourceManager* _resourceManager = nullptr;

		FW::FileWatcher _watcher;
		std::vector<FW::WatchID> _watches;
		AssetWatcherListener _listener;

	public:
		AssetWatcher() = default;
		~AssetWatcher() = default;

		void setResourceManager(ResourceManager& resourceManager) {
			_resourceManager = &resourceManager;
			_listener.setResourceManager(&resourceManager);
		}

		void startWatch(const std::filesystem::path& path) {
			auto p = (std::filesystem::current_path() / "../../src/shaderreload/shaders").lexically_normal();
			_watcher.addWatch(p.string(), &_listener);
		}

		void update() {
			_watcher.update();
		}
	};
}
