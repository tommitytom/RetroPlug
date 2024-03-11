#include "StyleCache.h"

#include <spdlog/spdlog.h>

#include "ui/next/CssUtil.h"
#include "ui/next/ReactElementView.h"

namespace fw {
	StyleCache::StyleCache() {
		CssUtil::setup(_parserLookup);
	}

	void StyleCache::load(const std::filesystem::path& path) {
		std::vector<Stylesheet> styles;
		
		if (!CssUtil::loadStyle(_parserLookup, path, styles)) {
			spdlog::error("Failed to load CSS file from {}", path.string());
			return;
		}

		for (auto it = _styles.begin(); it != _styles.end();) {
			if (it->filePath == path) {
				spdlog::warn("Stylesheet {} has already been loaded - this may cause style ordering issues", path.string());
				it = _styles.erase(it);
			} else {
				++it;
			}
		}

		_styles.insert(_styles.end(), styles.begin(), styles.end());
	}

	bool selectorItemMatches(const ReactElementView* view, const Selector::Item& item) {
		switch (item.type) {
		case SelectorType::IdSelector:
		{
			std::string_view id = view->getId();
			return !id.empty() && item.name == id;
		}
		case SelectorType::ClassSelector:
		{
			return view->getClassName().find(item.name) != std::string::npos;
		}
		case SelectorType::TypeSelector:
		{
			std::string_view name = view->getElementName();
			return !name.empty() && item.name == name;
		}
		case SelectorType::PseudoClassSelector:
		{
			if (item.name == "hover") {
				return !!(view->getStateFlag() & InputStateFlag::Hover);
			} else if (item.name == "active") {
				return !!(view->getStateFlag() & InputStateFlag::Active);
			} else {
				spdlog::warn("CSS psuedo class selector of type {} not implemented", item.name);
			}
		}
		}

		return false;
	}

	bool selectorMatches(const ReactElementView* view, const Selector& selector) {
		for (const Selector::Item& selectorItem : selector.items) {
			if (!selectorItemMatches(view, selectorItem)) {
				return false;
			}
		}

		return true;
	}

	const ReactElementView* selectorMatchesDescendant(const ReactElementView* view, const Selector& selector, bool recurse) {
		ViewPtr parent = view->getParent();
		while (parent != nullptr && parent->isType<ReactView>()) {
			const ReactElementView* parentElement = parent->asShared<ReactElementView>().get();

			if (selectorMatches(parentElement, selector)) {
				return parentElement;
			}

			if (recurse) {
				parent = parentElement->getParent();
			} else {
				return nullptr;
			}
		}

		return nullptr;
	}

	const ReactElementView* selectorMatchesSibling(const ReactElementView* view, const Selector& selector, bool recurse) {
		//assert(false);
		return nullptr;
	}

	bool isSelected(const ReactElementView* view, const SelectorGroup& selectorGroup) {
		for (const Selector& selector : selectorGroup.selectors) {
			switch (selector.combinator) {
			case 0:
			{
				if (!selectorMatches(view, selector)) { return false; }
				break;
			}
			case ' ':
			case '>':
			{
				const ReactElementView* match = selectorMatchesDescendant(view, selector, selector.combinator == ' ');
				if (match == nullptr) { return false; }
				view = match;
				break;
			}
			case '+':
			case '~':
			{
				const ReactElementView* match = selectorMatchesSibling(view, selector, selector.combinator == '~');
				if (match == nullptr) { return false; }
				view = match;
				break;
			}
			}
		}

		return true;
	}

	bool sortStyles(const Stylesheet* l, const Stylesheet* r) {
		const Specificity& lSpec = l->selectorGroup.specificity;
		const Specificity& rSpec = r->selectorGroup.specificity;

		if (lSpec.a != rSpec.a) {
			return lSpec.a > rSpec.a;
		} else if (lSpec.b != rSpec.b) {
			return lSpec.b > rSpec.b;
		} else if (lSpec.c != rSpec.c) {
			return lSpec.c > rSpec.c;
		}

		// When 2 rules have the same specificity, the one that appears later in the file is given prominence
		return lSpec.index > rSpec.index;
	}
	
	void StyleCache::getRules(const ReactElementView* view, std::vector<std::shared_ptr<StylesheetRule>>& target) {
		assert(target.size() == 1);

		for (const Stylesheet& selector : _styles) {
			if (isSelected(view, selector.selectorGroup)) {
				_styleScratch.insert(_styleScratch.begin(), &selector);
			}
		}

		std::sort(_styleScratch.begin(), _styleScratch.end(), sortStyles);

		for (const Stylesheet* stylesheet : _styleScratch) {
			target.push_back(stylesheet->rule);
		}

		_styleScratch.clear();
	}
}
