#pragma once

#include <memory>
#include <vector>
#include <filesystem>

#include "core/SystemHook.h"

namespace fw {
	class EventNode;
}

namespace rp {
	struct ProjectConfig {
		f32 zoom = 3.0f;
	};

	struct ProjectPathContext {
		std::filesystem::path projectPath = "";
		std::filesystem::path projectRoot = "";

		std::filesystem::path mountPath = "";
		std::filesystem::path tempPath = mountPath / "temp";
	};

	struct RetroPlugProjectContext {
		fw::EventNode& eventNode;
		uint32 version = 0;
		std::vector<std::unique_ptr<SystemHookBase>> systemHooks;
		std::vector<std::unique_ptr<SystemHookBase>> serviceHooks;

		template <typename T>
		void addSystemHook() {
			static_assert(std::is_base_of_v<SystemHookBase, T>, "T must be derived from SystemHookBase");
			systemHooks.push_back(std::make_unique<T>());
		}

		template <typename T>
		void addServiceHook() {
			static_assert(std::is_base_of_v<SystemHookBase, T>, "T must be derived from SystemHookBase");
			serviceHooks.push_back(std::make_unique<T>());
		}

		// Delete copy operations
		RetroPlugProjectContext(const RetroPlugProjectContext&) = delete;
		RetroPlugProjectContext& operator=(const RetroPlugProjectContext&) = delete;

		// Keep move operations (automatically generated)
		//RetroPlugProjectContext(RetroPlugProjectContext&&) = default;
		//RetroPlugProjectContext& operator=(RetroPlugProjectContext&&) = default;

		RetroPlugProjectContext(fw::EventNode& eventNode_): eventNode(eventNode_) {}
		~RetroPlugProjectContext() = default;
	};
}
