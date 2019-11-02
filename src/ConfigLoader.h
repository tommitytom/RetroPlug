#pragma once

#include <string>
#include <fstream>
#include "util/xstring.h"
#include "util/fs.h"
#include "util/File.h"
#include "platform/Path.h"

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"

static void saveButtonConfig(const tstring& path, const rapidjson::Document& source) {
	rapidjson::StringBuffer sb;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
	source.Accept(writer);
	std::ofstream configOut(path);
	if (configOut.is_open() && configOut.good()) {
		configOut.write(sb.GetString(), sb.GetSize());
	}
}

static void loadButtonConfig(rapidjson::Document& target, const tstring& file, const std::string& defaultConfigStr) {
	rapidjson::Document::AllocatorType& a = target.GetAllocator();

	tstring contentPath = getContentPath();
	if (!fs::exists(contentPath)) {
		fs::create_directory(contentPath);
	}

	rapidjson::Document defaultConfig(&a);
	defaultConfig.Parse(defaultConfigStr.c_str());

	tstring buttonPath = getContentPath(file);
	if (fs::exists(buttonPath)) {
		std::string buttonDataStr;
		if (readFile(tstr(buttonPath), buttonDataStr)) {
			target.Parse(buttonDataStr.c_str());

			bool obj = target.IsObject();

			auto gameboyConfig = target.FindMember("gameboy");
			auto lsdjConfig = target.FindMember("lsdj");

			if (gameboyConfig == target.MemberEnd() && lsdjConfig == target.MemberEnd()) {
				for (auto& v : target.GetObject()) {
					defaultConfig[v.name] = v.value;
				}

				target.CopyFrom(defaultConfig, a);
			}
		}
	} else {
		target.CopyFrom(defaultConfig, a);
	}

	saveButtonConfig(buttonPath, target);
}
