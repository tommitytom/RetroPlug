#pragma once

#include <filesystem>
#include <entt/entity/fwd.hpp>
#include <csspp/parser.h>
#include "Stylesheet.h"

namespace fw {
	//struct Stylesheet;
}

namespace fw::CssUtil {
	using ParseStyleFunc = std::function<void(const csspp::node::pointer_t&, std::vector<StylesheetRule::Property>&, size_t)>;
	using StyleParserLookup = std::unordered_map<std::string_view, ParseStyleFunc>;

	void setup(StyleParserLookup& lookup);

	bool loadStyle(const StyleParserLookup& lookup, const std::filesystem::path& path, std::vector<Stylesheet>& styleSheets);

	CssKeyword parseKeyword(std::string_view text);

	LengthType parseLength(std::string_view text);

	BorderStyleType parseBorderStyle(std::string_view text);
}