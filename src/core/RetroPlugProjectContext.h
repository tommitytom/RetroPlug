#pragma once

#include <memory>
#include <vector>
#include <filesystem>

#include "core/SystemHook.h"

namespace orb {
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
		std::unordered_map<orb::VirtualKey, orb::PadButtonType> keyboard;
		std::unordered_map<orb::PadButtonType, orb::PadButtonType> gamepad;

		static InputConfig defaultConfig() {
			InputConfig config;
			config.keyboard = {
				{ orb::VirtualKey::UpArrow, orb::PadButtonType::Up },
				{ orb::VirtualKey::DownArrow, orb::PadButtonType::Down },
				{ orb::VirtualKey::LeftArrow, orb::PadButtonType::Left },
				{ orb::VirtualKey::RightArrow, orb::PadButtonType::Right },
				{ orb::VirtualKey::D, orb::PadButtonType::A },
				{ orb::VirtualKey::W, orb::PadButtonType::B },
				{ orb::VirtualKey::Enter, orb::PadButtonType::Start },
				{ orb::VirtualKey::LeftShift, orb::PadButtonType::Select }
			};
			return config;
		}
	};

	struct RetroPlugProjectContext {
		orb::EventNode& eventNode;
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

		RetroPlugProjectContext(orb::EventNode& eventNode_): eventNode(eventNode_) {}
		~RetroPlugProjectContext() = default;
	};
}
