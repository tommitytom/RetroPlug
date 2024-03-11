#pragma once

#include <filesystem>
#include <unordered_map>

#include "ui/next/Stylesheet.h"
#include "ui/next/CssUtil.h"

namespace fw {
	class ReactElementView;

	class StyleCache {
	private:
		CssUtil::StyleParserLookup _parserLookup;
		std::vector<Stylesheet> _styles;

		std::vector<const Stylesheet*> _styleScratch;

	public:
		StyleCache();

		void clear() {
			_styles.clear();
		}

		void load(const std::filesystem::path& path);

		void getRules(const ReactElementView* view, std::vector<std::shared_ptr<StylesheetRule>>& target);
	};
}
