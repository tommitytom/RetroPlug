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

	struct HooksContext {
		HooksVector systemHooks;
		HooksVector serviceHooks;

		template <typename T>
		void addSystemHook() {
			static_assert(std::is_base_of_v<SystemHookBase, T>, "T must be derived from SystemHookBase");
			systemHooks.push_back(new T());
		}

		template <typename T>
		void addServiceHook() {
			static_assert(std::is_base_of_v<SystemHookBase, T>, "T must be derived from SystemHookBase");
			serviceHooks.push_back(new T());
		}
	};

	struct InputConfigData {
		std::unordered_map<std::string, std::string> keyboard;
		std::unordered_map<std::string, std::string> gamepad;
	};

	struct InputConfig {
		std::unordered_map<fw::VirtualKey, fw::PadButtonType> keyboard;
		std::unordered_map<fw::PadButtonType, fw::PadButtonType> gamepad;

		static InputConfig defaultConfig() {
			InputConfig config;
			config.keyboard = {
				{ fw::VirtualKey::UpArrow, fw::PadButtonType::Up },
				{ fw::VirtualKey::DownArrow, fw::PadButtonType::Down },
				{ fw::VirtualKey::LeftArrow, fw::PadButtonType::Left },
				{ fw::VirtualKey::RightArrow, fw::PadButtonType::Right },
				{ fw::VirtualKey::D, fw::PadButtonType::A },
				{ fw::VirtualKey::W, fw::PadButtonType::B },
				{ fw::VirtualKey::Enter, fw::PadButtonType::Start },
				{ fw::VirtualKey::LeftShift, fw::PadButtonType::Select }
			};
			return config;
		}
	};

	struct RetroPlugProjectContext {
		fw::EventNode& eventNode;
		uint32 version = 0;
		bool dirty = false;
		bool loading = false;
		bool requiresReset = false;

		void increaseVersion() {
			version++;
			dirty = true;
		}

		// Delete copy operations
		RetroPlugProjectContext(const RetroPlugProjectContext&) = delete;
		RetroPlugProjectContext& operator=(const RetroPlugProjectContext&) = delete;

		RetroPlugProjectContext(fw::EventNode& eventNode_): eventNode(eventNode_) {}
		~RetroPlugProjectContext() = default;
	};
}
