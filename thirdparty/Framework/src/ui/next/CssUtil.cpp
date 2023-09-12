#include "CssUtil.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include <csspp/assembler.h>
#include <csspp/compiler.h>
#include <csspp/exception.h>
#include <csspp/parser.h>
#include <spdlog/spdlog.h>

#include "ui/Flex.h"

#include "DocumentUtil.h"
#include "StyleComponents.h"
#include "Stylesheet.h"

namespace fw {
	struct CssSingleton {
		using ParseStyleFunc = std::function<void(const csspp::node::pointer_t&, std::vector<StylesheetRule::Property>&)>;
		using StyleParserLookup = std::unordered_map<std::string_view, ParseStyleFunc>;
		StyleParserLookup styleParsers;
	};

	template <typename T>
	std::string toString(const T& x) {
		std::ostringstream ss;
		ss << x;
		return ss.str();
	}

	template <typename T>
	T parsePropertyValue(const csspp::node::pointer_t& node);

	template <>
	Color4F parsePropertyValue<Color4F>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::COLOR));

		Color4F col;
		value->get_color().get_color(col.r, col.g, col.b, col.a);

		return col;
	}

	template <>
	FlexValue parsePropertyValue<FlexValue>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);

		switch (value->get_type()) {
		case csspp::node_type_t::INTEGER:
			return FlexValue(FlexUnit::Point, (f32)value->get_integer());
		case csspp::node_type_t::PERCENT:
			return FlexValue(FlexUnit::Percent, (f32)(value->get_decimal_number() * 100.0));
		case csspp::node_type_t::IDENTIFIER:
			if (value->get_string() == "auto") { return FlexValue(FlexUnit::Auto); }
			break;
		}

		assert(false);
		return FlexValue();
	}

	template <>
	std::string parsePropertyValue<std::string>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));
		
		return value->get_string();
	}

	template <>
	TimingFunction parsePropertyValue<TimingFunction>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		return TimingFunction{ TransitionTimingType::Linear };
	}

	template <>
	FontFamilyValue parsePropertyValue<FontFamilyValue>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		return FontFamilyValue{
			.familyName = "Arial"
		};
	}

	template <>
	LengthValue parsePropertyValue<LengthValue>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();
		csspp::decimal_number_t num = value->get_decimal_number();
		
		return LengthValue{
			.type = LengthType::Pixel,
			.value = (f32)num
		};
	}

	template <>
	FontWeightValue parsePropertyValue<FontWeightValue>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();
		csspp::decimal_number_t num = value->get_decimal_number();

		return FontWeightValue{
			.value = (f32)num
		};
	}

	template <>
	TextAlignType parsePropertyValue<TextAlignType>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();

		return TextAlignType::Center;
	}

	template <>
	std::chrono::duration<f32> parsePropertyValue<std::chrono::duration<f32>>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		
		f32 time = 0;
		std::string_view timeRep;
		
		switch (value->get_type()) {
			case csspp::node_type_t::INTEGER:
				time = (f32)value->get_integer();
				timeRep = value->get_string();
				break;
			case csspp::node_type_t::DECIMAL_NUMBER:
				time = (f32)value->get_decimal_number();
				timeRep = value->get_string();
				break;
		}

		if (timeRep == "s") {
			return std::chrono::duration<f32>{ time };
		} else if (timeRep == "ms") {
			return std::chrono::duration<f32, std::milli>{ time };
		}

		assert(false);
		return std::chrono::duration<f32>{};
	}

	template <typename T>
	void parseProperty(const csspp::node::pointer_t& node, std::vector<StylesheetRule::Property>& items) {
		using ValueType = decltype(T::value);
		items.push_back(StylesheetRule::Property{
			.name = T::PropertyName,
			.data = T { parsePropertyValue<ValueType>(node) },
			.set = &propertySetter<T>,
		});
	}


	void parseSelectors(const csspp::node::pointer_t& items, std::vector<Selector>& selectors) {
		Selector* current = &selectors.emplace_back();

		for (size_t i = 0; i < items->size(); ++i) {
			const auto& item = items->get_child(i);

			if (item->is(csspp::node_type_t::PERIOD)) {
				current->items.push_back(Selector::Item{ .type = SelectorType::ClassSelector, .name = items->get_child(++i)->get_string() });
			} else if (item->is(csspp::node_type_t::HASH)) {
				current->items.push_back(Selector::Item{ .type = SelectorType::IdSelector, .name = items->get_child(i)->get_string() });
			} else if (item->is(csspp::node_type_t::IDENTIFIER)) {
				current->items.push_back(Selector::Item{ .type = SelectorType::TypeSelector, .name = items->get_child(i)->get_string() });
			} else if (item->is(csspp::node_type_t::COLON)) {
				current->items.push_back(Selector::Item{ .type = SelectorType::PseudoClassSelector, .name = items->get_child(++i)->get_string() });
			} else if (item->is(csspp::node_type_t::WHITESPACE)) {
				current->combinator = ' ';
				current = &(*selectors.emplace(selectors.begin()));
			} else if (item->is(csspp::node_type_t::GREATER_THAN)) {
				current->combinator = '>';
				current = &(*selectors.emplace(selectors.begin()));
			} else if (item->is(csspp::node_type_t::ADD)) {
				current->combinator = '+';
				current = &(*selectors.emplace(selectors.begin()));
			} else if (item->is(csspp::node_type_t::PRECEDED)) {
				current->combinator = '~';
				current = &(*selectors.emplace(selectors.begin()));
			} else {
				spdlog::warn("Encountered unsupported CSS selector of type {}", toString(item->get_type()));
				//assert(false);
			}
		}
	}

	Specificity calculateSpecificity(const std::vector<Selector>& selectors) {
		return Specificity{};
	}

	void parseSingleProperty(const csspp::node::pointer_t& node, const CssSingleton::StyleParserLookup& parserLookup, StylesheetRule& rule) {
		assert(node->is(csspp::node_type_t::DECLARATION));

		const std::string& name = node->get_string();
		auto found = parserLookup.find(name);

		if (found != parserLookup.end()) {
			found->second(node, rule.properties);
		} else {
			spdlog::warn("CSS selector of type {} is not supported", name);
		}
	}

	void parseProperties(const csspp::node::pointer_t& node, const CssSingleton::StyleParserLookup& parserLookup, StylesheetRule& rule) {
		assert(node->is(csspp::node_type_t::LIST) || node->is(csspp::node_type_t::DECLARATION));

		if (node->is(csspp::node_type_t::LIST)) {
			for (size_t i = 0; i < node->size(); ++i) {
				auto decl = node->get_child(i);
				parseSingleProperty(node->get_child(i), parserLookup, rule);
			}
		} else if (node->is(csspp::node_type_t::DECLARATION)) {
			parseSingleProperty(node, parserLookup, rule);
		}
	}

	bool CssUtil::loadStyle(const entt::registry& reg, const std::filesystem::path& path, std::vector<Stylesheet>& styleSheets) {
		const CssSingleton& cssSingleton = reg.ctx().at<CssSingleton>();

		const int f_precision = 3;

		std::ifstream in(path);
		csspp::position::pointer_t pos = std::make_shared<csspp::position>(path.filename().string());
		csspp::lexer::pointer_t l = std::make_shared<csspp::lexer>(in, *pos);

		csspp::safe_precision_t safePrecision(f_precision);
		csspp::error_happened_t errorTracker;
		csspp::parser p(l);
		csspp::node::pointer_t root = p.stylesheet();

		csspp::compiler compiler;
		compiler.set_root(root);
		compiler.add_path(std::filesystem::current_path().string());
		compiler.compile(true);

		std::cout << *root << std::endl;

		for (size_t i = 0; i < root->size(); ++i) {
			const auto& styleClass = root->get_child(i);
			if (!styleClass->is(csspp::node_type_t::COMPONENT_VALUE)) {
				continue;
			}

			std::shared_ptr<StylesheetRule> rule = std::make_shared<StylesheetRule>();
			std::vector<SelectorGroup> selectors;

			// Parse selectors until we hit the curly bracket
			for (size_t j = 0; j < styleClass->size(); ++j) {
				const auto item = styleClass->get_child(j);

				if (item->is(csspp::node_type_t::ARG)) {
					SelectorGroup& selector = selectors.emplace_back();
					parseSelectors(item, selector.selectors);
					selector.specificity = calculateSpecificity(selector.selectors);
				} else if (item->is(csspp::node_type_t::OPEN_CURLYBRACKET)) {					
					parseProperties(item->get_child(0), cssSingleton.styleParsers, *rule);
					break;
				} else {
					spdlog::warn("Encountered unsupported node of type {} when parsing stylesheet", toString(item->get_type()));
				}
			}

			for (SelectorGroup& selector : selectors) {
				styleSheets.push_back(Stylesheet{ std::move(selector), rule });
			}
		}

		return true;
	}
	
	template <typename T>
	constexpr std::pair<std::string_view, CssSingleton::ParseStyleFunc> makeStyleParser() {
		return std::make_pair(T::PropertyName, parseProperty<T>);
	}

	void CssUtil::setup(entt::registry& reg) {
		reg.ctx().emplace<CssSingleton>(CssSingleton{
			.styleParsers = {
				makeStyleParser<styles::BackgroundColor>(),
				makeStyleParser<styles::Color>(),
				makeStyleParser<styles::Height>(),
				makeStyleParser<styles::TransitionDelay>(),
				makeStyleParser<styles::TransitionDuration>(),
				makeStyleParser<styles::TransitionProperty>(),
				makeStyleParser<styles::TransitionTimingFunction>(),
				makeStyleParser<styles::FontFamily>(),
				makeStyleParser<styles::FontSize>(),
				makeStyleParser<styles::FontWeight>(),
				makeStyleParser<styles::TextAlign>(),
				makeStyleParser<styles::Width>(),
			}
		});
	}
}
