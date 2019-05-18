#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <tao/json.hpp>
#include "util/String.h"
#include "platform/Path.h"

const std::string DEFAULT_BUTTON_CONFIG = "{\"gameboy\":{\"A\":\"Z\",\"B\":\"X\",\"Up\":\"UpArrow\",\"Down\":\"DownArrow\",\"Left\":\"LeftArrow\",\"Right\":\"RightArrow\",\"Select\":\"Ctrl\",\"Start\":\"Enter\"},\"lsdj\":{\"ScreenUp\":\"W\",\"ScreenDown\":\"S\",\"ScreenLeft\":\"A\",\"ScreenRight\":\"D\",\"DownTenRows\":\"PageDown\",\"UpTenRows\":\"PageUp\",\"CancelSelection\":\"Esc\"}}";

static void saveButtonConfig(const std::string& path, const tao::json::value& source) {
	std::ofstream configOut(path);
	tao::json::to_stream(configOut, source, 2);
}

static void loadButtonConfig(tao::json::value& target) {
	std::string contentPath = getContentPath();
	if (!std::filesystem::exists(contentPath)) {
		std::filesystem::create_directory(contentPath);
	}

	tao::json::value defaultConfig = tao::json::from_string(DEFAULT_BUTTON_CONFIG);

	std::string buttonPath = getContentPath("buttons.json");
	if (std::filesystem::exists(buttonPath)) {
		tao::json::value fileData = tao::json::parse_file(buttonPath);

		bool obj = fileData.is_object();

		auto gameboyConfig = fileData.find("gameboy");
		auto lsdjConfig = fileData.find("lsdj");
		
		if (!gameboyConfig && !lsdjConfig) {
			defaultConfig.at("gameboy").swap(fileData);
			target = defaultConfig;
		} else {
			target = fileData;
		}
	} else {
		target = defaultConfig;
	}

	saveButtonConfig(buttonPath, target);
}
