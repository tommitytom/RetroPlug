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
//#include "ui/ViewLayout.h"

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
	f32 parsePropertyValue<f32>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);

		switch (value->get_type()) {
		case csspp::node_type_t::INTEGER:
			return (f32)value->get_integer();
		case csspp::node_type_t::DECIMAL_NUMBER:
			return (f32)value->get_decimal_number();
		}

		spdlog::warn("Failed to parse number value: {}", value->to_string(0));
		return 0.0f;
	}

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

		//value->get_dim1

		return FontFamilyValue{
			.familyName = "Arial"
		};
	}

	template <>
	LengthValue parsePropertyValue<LengthValue>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::INTEGER));

		const std::string& str = value->get_string();
		csspp::integer_t num = value->get_integer();
		
		return LengthValue{
			.type = LengthType::Pixel,
			.value = (f32)num,
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

		if (str == "start") { return TextAlignType::Start; }
		if (str == "end") { return TextAlignType::End; }
		if (str == "left") { return TextAlignType::Left; }
		if (str == "right") { return TextAlignType::Right; }
		if (str == "center") { return TextAlignType::Center; }
		if (str == "justify") { return TextAlignType::Justify; }
		if (str == "match-parent") { return TextAlignType::MatchParent; }
		if (str == "justify-all") { return TextAlignType::JustifyAll; }

		spdlog::warn("Failed to parse text-align value: {}", str);
		return TextAlignType::Left;
	}

	template <>
	FlexPositionType parsePropertyValue<FlexPositionType>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();

		if (str == "static") { return FlexPositionType::Static; }
		if (str == "relative") { return FlexPositionType::Relative; }
		if (str == "absolute") { return FlexPositionType::Absolute; }

		spdlog::warn("Failed to parse position value: {}", str);
		return FlexPositionType::Static;
	}

	template <>
	FlexOverflow parsePropertyValue<FlexOverflow>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();

		if (str == "hidden") { return FlexOverflow::Hidden; }
		if (str == "scroll") { return FlexOverflow::Scroll; }
		if (str == "visible") { return FlexOverflow::Visible; }

		spdlog::warn("Failed to parse overflow value: {}", str);
		return FlexOverflow::Visible;
	}

	template <>
	FlexJustify parsePropertyValue<FlexJustify>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));
		
		const std::string& str = value->get_string();

		if (str == "flex-start") { return FlexJustify::FlexStart; }
		if (str == "center") { return FlexJustify::Center; }
		if (str == "flex-end") { return FlexJustify::FlexEnd; }
		if (str == "space-between") { return FlexJustify::SpaceBetween; }
		if (str == "space-around") { return FlexJustify::SpaceAround; }
		if (str == "space-evenly") { return FlexJustify::SpaceEvenly; }

		spdlog::warn("Failed to parse justify value: {}", str);
		return FlexJustify::FlexStart;
	}

	template <>
	FlexAlign parsePropertyValue<FlexAlign>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();

		if (str == "auto") { return FlexAlign::Auto; }
		if (str == "flex-start") { return FlexAlign::FlexStart; }
		if (str == "center") { return FlexAlign::Center; }
		if (str == "flex-end") { return FlexAlign::FlexEnd; }
		if (str == "stretch") { return FlexAlign::Stretch; }
		if (str == "baseline") { return FlexAlign::Baseline; }
		if (str == "space-between") { return FlexAlign::SpaceBetween; }
		if (str == "space-around") { return FlexAlign::SpaceAround; }

		spdlog::warn("Failed to parse align value: {}", str);
		return FlexAlign::Auto;
	}

	template <>
	fw::FlexWrap parsePropertyValue<fw::FlexWrap>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();

		if (str == "no-wrap") { return FlexWrap::NoWrap; }
		if (str == "wrap") { return FlexWrap::Wrap; }
		if (str == "wrap-reverse") { return FlexWrap::WrapReverse; }

		spdlog::warn("Failed to parse wrap value: {}", str);
		return fw::FlexWrap::NoWrap;
	}

	template <>
	FlexDirection parsePropertyValue<FlexDirection>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();
		
		if (str == "column") { return FlexDirection::Column; }
		if (str == "column-reverse") { return FlexDirection::ColumnReverse; }
		if (str == "row") { return FlexDirection::Row; }
		if (str == "row-reverse") { return FlexDirection::RowReverse; }

		spdlog::warn("Failed to parse direction value: {}", str);
		return FlexDirection::Row;
	}

	template <>
	BorderStyleType parsePropertyValue<BorderStyleType>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();

		if (str == "none") { return BorderStyleType::None; }
		if (str == "hidden") { return BorderStyleType::Hidden; }
		if (str == "dotted") { return BorderStyleType::Dotted; }
		if (str == "dashed") { return BorderStyleType::Dashed; }
		if (str == "solid") { return BorderStyleType::Solid; }
		if (str == "double") { return BorderStyleType::Double; }
		if (str == "groove") { return BorderStyleType::Groove; }
		if (str == "ridge") { return BorderStyleType::Ridge; }
		if (str == "inset") { return BorderStyleType::Inset; }
		if (str == "outset") { return BorderStyleType::Outset; }

		spdlog::warn("Failed to parse border style value: {}", str);
		return BorderStyleType::Solid;
	}

	template <>
	CursorType parsePropertyValue<CursorType>(const csspp::node::pointer_t& node) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		const auto& value = arg->get_child(0);
		assert(value->is(csspp::node_type_t::IDENTIFIER));

		const std::string& str = value->get_string();

		if (str == "auto") { return CursorType::Arrow; }
		if (str == "default") { return CursorType::Arrow; }
		if (str == "none") {return CursorType::Arrow; }
		if (str == "context-menu") {return CursorType::Arrow; }
		if (str == "help") {return CursorType::Arrow; }
		if (str == "pointer") {return CursorType::Arrow; }
		if (str == "progress") {return CursorType::Arrow; }
		if (str == "wait") {return CursorType::Arrow; }
		if (str == "cell") {return CursorType::Arrow; }
		if (str == "crosshair") {return CursorType::Crosshair; }
		if (str == "text") {return CursorType::IBeam; }
		if (str == "vertical-text") {return CursorType::IBeam; }
		if (str == "alias") {return CursorType::Arrow; }
		if (str == "copy") {return CursorType::Arrow; }
		if (str == "move") {return CursorType::Arrow; }
		if (str == "no-drop") {return CursorType::Arrow; }
		if (str == "not-allowed") {return CursorType::Arrow; }
		if (str == "grab") {return CursorType::Hand; }
		if (str == "grabbing") {return CursorType::Hand; }
		if (str == "e-resize") {return CursorType::ResizeEW; }
		if (str == "n-resize") {return CursorType::ResizeNS; }
		if (str == "ne-resize") {return CursorType::ResizeNE; }
		if (str == "nw-resize") {return CursorType::ResizeNW; }
		if (str == "s-resize") {return CursorType::ResizeNS; }
		if (str == "se-resize") {return CursorType::ResizeSE; }
		if (str == "sw-resize") {return CursorType::ResizeSW; }
		if (str == "w-resize") {return CursorType::ResizeEW; }
		if (str == "ew-resize") {return CursorType::ResizeEW; }
		if (str == "ns-resize") {return CursorType::ResizeNS; }
		if (str == "nesw-resize") {return CursorType::ResizeNESW; }
		if (str == "nwse-resize") {return CursorType::ResizeNWSE; }
		if (str == "col-resize") {return CursorType::ResizeEW; }
		if (str == "row-resize") {return CursorType::ResizeNS; }
		if (str == "all-scroll") {return CursorType::Arrow; }
		if (str == "zoom-in") {return CursorType::Arrow; }
		if (str == "zoom-out") {return CursorType::Arrow; }

		spdlog::warn("Failed to parse border style value: {}", str);
		return CursorType::Arrow;
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

	void parseBorder(const csspp::node::pointer_t& node, std::vector<StylesheetRule::Property>& items) {
		const auto& arg = node->get_child(0);
		assert(arg->is(csspp::node_type_t::ARG));
		
		size_t paramCount = 0;
		for (size_t i = 0; i < arg->size(); ++i) {
			if (!arg->get_child(i)->is(csspp::node_type_t::WHITESPACE)) {
				paramCount++;
			}
		}

		//std::cout << "-------" << std::endl;
		//std::cout << *node << std::endl;
		//std::cout << "-------" << std::endl;
		
		/*items.push_back(StylesheetRule::Property{
			.name = "border-top-width",
			.data = T { parsePropertyValue<ValueType>(node) },
			.set = &propertySetter<T>,
		});

		items.push_back(StylesheetRule::Property{
			.name = "border-left-width",
			.data = T { parsePropertyValue<ValueType>(node) },
			.set = &propertySetter<T>,
		});

		items.push_back(StylesheetRule::Property{
			.name = "border-bottom-width",
			.data = T { parsePropertyValue<ValueType>(node) },
			.set = &propertySetter<T>,
		});

		items.push_back(StylesheetRule::Property{
			.name = "border-right-width",
			.data = T { parsePropertyValue<ValueType>(node) },
			.set = &propertySetter<T>,
		});*/
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
	
	void traverse(const Selector& node, Specificity& result) {
		for (auto item : node.items) {
			switch (item.type) {
			case SelectorType::IdSelector: 
				++result.a; 
				break;
			case SelectorType::AttributeSelector:
			case SelectorType::ClassSelector:
				++result.b;
				break;
			case SelectorType::PseudoElementSelector:
				++result.c;
				break;
			case SelectorType::PseudoClassSelector: 
				++result.b;
				break;
			}
		}
	}

	void calculateSpecificity(const std::vector<Selector>& selectors, Specificity& result) {
		for (const Selector& selector : selectors) {
			for (const Selector::Item& item : selector.items) {
				switch (item.type) {
				case SelectorType::IdSelector:
					++result.a;
					break;
				case SelectorType::AttributeSelector:
				case SelectorType::ClassSelector:
				case SelectorType::PseudoClassSelector:
					++result.b;
					break;
				case SelectorType::TypeSelector:
				case SelectorType::PseudoElementSelector:
					++result.c;
					break;
				}
			}
		}
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
					SelectorGroup& selector = selectors.emplace_back(Specificity{ j });
					parseSelectors(item, selector.selectors);
					calculateSpecificity(selector.selectors, selector.specificity);
				} else if (item->is(csspp::node_type_t::OPEN_CURLYBRACKET)) {					
					parseProperties(item->get_child(0), cssSingleton.styleParsers, *rule);
					break;
				} else {
					spdlog::warn("Encountered unsupported node of type {} when parsing stylesheet", toString(item->get_type()));
				}
			}

			for (SelectorGroup& selector : selectors) {
				styleSheets.push_back(Stylesheet{ path, std::move(selector), rule });
			}
		}

		return true;
	}
	
	template <typename T>
	constexpr std::pair<std::string_view, CssSingleton::ParseStyleFunc> makeStyleParser() {
		return std::make_pair(T::PropertyName, parseProperty<T>);
	}

	template <auto Func>
	constexpr std::pair<std::string_view, CssSingleton::ParseStyleFunc> makeCompositeStyleParser(std::string_view name) {
		return std::make_pair(name, Func);
	}

	void CssUtil::setup(entt::registry& reg) {
		reg.ctx().emplace<CssSingleton>(CssSingleton{
			.styleParsers = {
				makeStyleParser<styles::Cursor>(),
				makeStyleParser<styles::Color>(),
				makeStyleParser<styles::BackgroundColor>(),

				makeStyleParser<styles::MarginBottom>(),
				makeStyleParser<styles::MarginTop>(),
				makeStyleParser<styles::MarginLeft>(),
				makeStyleParser<styles::MarginRight>(),

				makeStyleParser<styles::PaddingBottom>(),
				makeStyleParser<styles::PaddingTop>(),
				makeStyleParser<styles::PaddingLeft>(),
				makeStyleParser<styles::PaddingRight>(),

				makeStyleParser<styles::BorderBottomWidth>(),
				makeStyleParser<styles::BorderTopWidth>(),
				makeStyleParser<styles::BorderLeftWidth>(),
				makeStyleParser<styles::BorderRightWidth>(),
				makeStyleParser<styles::BorderBottomColor>(),
				makeStyleParser<styles::BorderTopColor>(),
				makeStyleParser<styles::BorderLeftColor>(),
				makeStyleParser<styles::BorderRightColor>(),
				makeStyleParser<styles::BorderBottomStyle>(),
				makeStyleParser<styles::BorderTopStyle>(),
				makeStyleParser<styles::BorderLeftStyle>(),
				makeStyleParser<styles::BorderRightStyle>(),

				// flex-flow
				makeStyleParser<styles::FlexDirection>(),
				makeStyleParser<styles::FlexWrap>(),

				makeStyleParser<styles::FlexBasis>(),
				makeStyleParser<styles::FlexGrow>(),
				makeStyleParser<styles::FlexShrink>(),
				makeStyleParser<styles::AlignItems>(),
				makeStyleParser<styles::AlignContent>(),
				makeStyleParser<styles::AlignSelf>(),
				makeStyleParser<styles::JustifyContent>(),
				makeStyleParser<styles::Overflow>(),

				makeStyleParser<styles::Position>(),
				makeStyleParser<styles::Top>(),
				makeStyleParser<styles::Left>(),
				makeStyleParser<styles::Bottom>(),
				makeStyleParser<styles::Right>(),

				makeStyleParser<styles::Width>(),
				makeStyleParser<styles::Height>(),
				makeStyleParser<styles::MinWidth>(),
				makeStyleParser<styles::MinHeight>(),
				makeStyleParser<styles::MaxWidth>(),
				makeStyleParser<styles::MaxHeight>(),

				makeStyleParser<styles::TransitionProperty>(),
				makeStyleParser<styles::TransitionDuration>(),
				makeStyleParser<styles::TransitionTimingFunction>(),
				makeStyleParser<styles::TransitionDelay>(),
				makeStyleParser<styles::FontFamily>(),
				makeStyleParser<styles::FontSize>(),
				makeStyleParser<styles::FontWeight>(),
				makeStyleParser<styles::TextAlign>(),
			}
		});
	}
}
