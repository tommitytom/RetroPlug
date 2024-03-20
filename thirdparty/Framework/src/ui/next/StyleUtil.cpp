#include "StyleUtil.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include <spdlog/spdlog.h>

#include "ui/Flex.h"

#include "DocumentUtil.h"
#include "StyleComponents.h"
#include "Stylesheet.h"
#include "Transitions.h"
#include "graphics/FontManager.h"

namespace fw {
	struct Animator {
		using AnimatorFunc = std::function<void(DomElementHandle, DomElementHandle)>;
		DomElementHandle sourceStyle = entt::null;
		DomElementHandle targetStyle = entt::null;
		AnimatorFunc func;
	};

	struct StyleSingleton {
		std::vector<Stylesheet> selectors;
		std::unordered_map<std::string_view, PropertyVTable> propMethods;
	};

	bool selectorItemMatches(const entt::registry& reg, const DomElementHandle handle, const Selector::Item& item) {
		switch (item.type) {
			case SelectorType::IdSelector: {
				const IdComponent* id = reg.try_get<IdComponent>(handle);
				return id && item.name == id->value;
			}
			case SelectorType::ClassSelector: {
				const StyleClassComponent* style = reg.try_get<StyleClassComponent>(handle);
				if (style) {
					return std::find(style->classNames.begin(), style->classNames.end(), item.name) != style->classNames.end();
				}
				
				return false;
			}
			case SelectorType::TypeSelector: {
				return reg.get<ElementTag>(handle).tag == item.name;
			}
			case SelectorType::PseudoClassSelector: {
				if (item.name == "hover") {
					return reg.all_of<MouseEnteredTag>(handle);
				}  else if (item.name == "active") {
					return reg.all_of<MouseFocusTag>(handle);
				} else {
					spdlog::warn("CSS psuedo class selector of type {} not implemented", item.name);
				}
			}
		}

		return false;
	}

	bool selectorMatches(const entt::registry& reg, const DomElementHandle handle, const Selector& selector) {
		for (const Selector::Item& selectorItem : selector.items) {
			if (!selectorItemMatches(reg, handle, selectorItem)) {
				return false;
			}
		}
		
		return true;
	}

	DomElementHandle selectorMatchesDescendant(const entt::registry& reg, const DomElementHandle handle, const Selector& selector, bool recurse) {
		DomElementHandle parent = DocumentUtil::getParent(reg, handle);

		while (parent != entt::null) {
			if (selectorMatches(reg, parent, selector)) {
				return parent;
			}

			if (recurse) {
				parent = DocumentUtil::getParent(reg, parent);
			} else {
				return entt::null;
			}
		}

		return entt::null;
	}

	DomElementHandle selectorMatchesSibling(const entt::registry& reg, const DomElementHandle handle, const Selector& selector, bool recurse) {
		//assert(false);
		return entt::null;
	}

	bool isSelected(const entt::registry& reg, DomElementHandle handle, const SelectorGroup& selectorGroup) {
		for (const Selector& selector : selectorGroup.selectors) {
			switch (selector.combinator) {
				case 0: {
					if (!selectorMatches(reg, handle, selector)) { return false; }
					break;
				}
				case ' ': 
				case '>': {
					const DomElementHandle match = selectorMatchesDescendant(reg, handle, selector, selector.combinator == ' ');
					if (match == entt::null) { return false; }
					handle = match;
					break;
				}
				case '+':
				case '~': {
					const DomElementHandle match = selectorMatchesSibling(reg, handle, selector, selector.combinator == '~');
					if (match == entt::null) { return false; }
					handle = match;
					break;
				}
			}
		}
		
		return true;
	}

	void StyleUtil::addStyleSheets(entt::registry& reg, const std::filesystem::path& path, std::vector<Stylesheet>&& stylesheets) {
		StyleSingleton& styleSingleton = reg.ctx().at<StyleSingleton>();

		for (auto it = styleSingleton.selectors.begin(); it != styleSingleton.selectors.end();) {
			if (it->filePath == path) {
				it = styleSingleton.selectors.erase(it);
			} else {
				++it;
			}
		}	

		styleSingleton.selectors.insert(styleSingleton.selectors.end(), stylesheets.begin(), stylesheets.end());

		// Force all nodes to update their styles
		reg.view<StyleReferences>().each([&](entt::entity e) { reg.emplace_or_replace<StyleDirtyTag>(e); });
	}

	StyleHandle StyleUtil::createEmptyStyle(entt::registry& reg, const DomElementHandle element) {
		StyleHandle style = reg.create();
		reg.emplace<ElementReferenceComponent>(style, element);
		return style;
	}

	void StyleUtil::markStyleDirty(entt::registry& reg, const DomElementHandle handle, bool recurse) {
		reg.emplace_or_replace<StyleDirtyTag>(handle);
		
		if (recurse) {
			DocumentUtil::each(reg, handle, [&](const DomElementHandle child) {
				markStyleDirty(reg, child, true);
			});
		}
	}

	template <typename T>
	void inheritProperty(entt::registry& reg, const DomElementHandle sourceElement, const StyleHandle targetStyle, const T& def) {
		const T* prop = StyleUtil::findProperty<T>(reg, sourceElement, false);
		if (prop) {
			reg.emplace<T>(targetStyle, *prop);
		} else {
			reg.emplace<T>(targetStyle, def);
		}
	}

	StyleHandle createTextStyle(entt::registry& reg, const DomElementHandle handle) {
		StyleHandle style = StyleUtil::createEmptyStyle(reg, handle);
		
		inheritProperty<styles::FontFamily>(reg, handle, style, styles::FontFamily{ FontFamilyValue { "Karla-Regular" } });
		inheritProperty<styles::FontSize>(reg, handle, style, styles::FontSize{ LengthValue { LengthType::Default, 16 } });
		inheritProperty<styles::FontWeight>(reg, handle, style, styles::FontWeight {});

		return style;
	}

	bool sortRules(const Stylesheet* l, const Stylesheet* r) {
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

	StyleHandle createStyle(entt::registry& reg, const DomElementHandle handle) {
		const TextComponent* text = reg.try_get<TextComponent>(handle);
		if (text) {
			return createTextStyle(reg, handle);
		}

		StyleReferences& styleRef = reg.get<StyleReferences>(handle);
		const StyleSingleton& styleSingleton = reg.ctx().at<StyleSingleton>();
		std::vector<const Stylesheet*> rules;
		std::unordered_map<entt::id_type, const StylesheetRule::Property*> props;

		for (const Stylesheet& selector : styleSingleton.selectors) {
			if (isSelected(reg, handle, selector.selectorGroup)) {
				rules.insert(rules.begin(), &selector);
			}
		}

		std::sort(rules.begin(), rules.end(), sortRules);

		for (auto it = rules.rbegin(); it != rules.rend(); ++it) {
			for (const StylesheetRule::Property& prop : (*it)->rule->properties) {
				props[prop.data.type().index()] = &prop;
			}
		}

		const StylesheetRule* inlineStyle = reg.try_get<StylesheetRule>(handle);
		if (inlineStyle) {
			for (const StylesheetRule::Property& prop : inlineStyle->properties) {
				props[prop.data.type().index()] = &prop;
			}
		}

		StyleHandle style = StyleUtil::createEmptyStyle(reg, handle);

		for (const auto& [k, v] : props) {
			v->set(reg, style, v->data);
		}
		
		return style;
	}

	void splitString(std::string_view str, char delim, std::vector<std::string_view>& target) {
		size_t start = 0;
		size_t end = start;
		
		for (size_t i = 0; i < str.size(); ++i) {
			if (str[i] != delim) {
				++end;
			} else if (end - start > 0) {
				target.push_back(str.substr(start, end));
				start = i + 1;
				end = start;
			}
		}

		if (end - start > 0) {
			target.push_back(str.substr(start, end));
		}
	}

	template <typename T>
	entt::id_type getStyleId() {
		return entt::type_id<T>().index();
	}

	bool initializeTransitions(entt::registry& reg, const DomElementHandle element, StyleReferences& styleRef, const std::vector<std::string_view>& propNames) {
		const StyleSingleton& styleSingleton = reg.ctx().at<StyleSingleton>();
		bool valid = false;
		
		for (std::string_view propName : propNames) {
			auto found = styleSingleton.propMethods.find(propName);
			if (found != styleSingleton.propMethods.end()) {
				if (!found->second.transition) {
					spdlog::warn("Failed to create transition for {}", propName);
					continue;
				}

				const styles::TransitionDuration* transDur = reg.try_get<styles::TransitionDuration>(styleRef.current);
				const styles::TransitionTimingFunction* transType = reg.try_get<styles::TransitionTimingFunction>(styleRef.current);
				
				assert(reg.valid(styleRef.current));
				assert(reg.valid(styleRef.from));
				assert(reg.valid(styleRef.to));

				if (!found->second.copy(reg, styleRef.current, styleRef.to)) {
					continue;
				}
				
				if (!found->second.copy(reg, styleRef.from, styleRef.current)) {
					continue;
				}
				
				entt::entity transEntity = reg.create();
				Transition& trans = reg.emplace<Transition>(transEntity, Transition{
					.parent = element,
					.from = styleRef.from,
					.to = styleRef.to,
					.target = styleRef.current,
					.func = found->second.transition,
					.state = Transition::State {
						.type = transType ? transType->value.type : TransitionTimingType::Linear,
						.duration = transDur ? transDur->value.count() : 1.0f,
					}
				});

				styleRef.transitions.push_back(transEntity);

				valid = true;
			} else {
				spdlog::warn("Failed to find a transition handler for property {}", propName);
			}
		}

		if (!valid) {
			// There were no valid transitions!
		}

		return valid;
	}

	void removeTransition(entt::registry& reg, const DomElementHandle elementHandle, const entt::entity transitionHandle) {
		StyleReferences& ref = reg.get<StyleReferences>(elementHandle);
		auto found = std::find(ref.transitions.begin(), ref.transitions.end(), transitionHandle);

		if (found != ref.transitions.end()) {
			ref.transitions.erase(found);

			if (ref.transitions.empty()) {
				reg.destroy(ref.from);
				reg.destroy(ref.to);
				ref.from = entt::null;
				ref.to = entt::null;
			}
		} else {
			spdlog::warn("Failed to remove transition (could not be found)");
		}

		reg.destroy(transitionHandle);
	}
	
	void removeAllTransitions(entt::registry& reg, StyleReferences& styleRef) {
		for (entt::entity trans : styleRef.transitions) {
			reg.destroy(trans);
		}

		styleRef.transitions.clear();

		if (styleRef.from != entt::null) {
			reg.destroy(styleRef.from);
			styleRef.from = entt::null;
		}

		if (styleRef.to != entt::null) {
			reg.destroy(styleRef.to);
			styleRef.to = entt::null;
		}
	}

	void removeAllTransitions(entt::registry& reg, DomElementHandle handle) {
		removeAllTransitions(reg, reg.get<StyleReferences>(handle));
	}

	void updateElementStyle(entt::registry& reg, DomElementHandle elementHandle, bool force) {
		if (reg.all_of<StyleDirtyTag>(elementHandle) || force) {
			StyleReferences& styleRef = reg.get<StyleReferences>(elementHandle);
			removeAllTransitions(reg, styleRef);

			assert(styleRef.prev == entt::null);
			
			styleRef.prev = styleRef.current;
			reg.remove<CurrentStyleTag, CurrentStyleDirtyTag>(styleRef.prev);
			
			styleRef.current = createStyle(reg, elementHandle);
			reg.emplace<LayoutDirtyTag>(styleRef.current);
			reg.emplace<CurrentStyleTag>(styleRef.current);
			reg.emplace<CurrentStyleDirtyTag>(styleRef.current);

			reg.emplace<StyleUpdatedTag>(elementHandle);

			// Once we enounter a dirty style, all child nodes also need to be updated
			force = true;
		}

		DocumentUtil::each(reg, elementHandle, [&](const DomElementHandle child) {
			updateElementStyle(reg, child, force);
		});
	}	

	void updateStyles(entt::registry& reg) {
		if (!reg.view<StyleDirtyTag>().empty()) {
			// There is a dirty style in the node tree somewhere
			// Start from the root node and recreate styles for nodes that have StyleDirtyTag
			// This could be way more efficient (like sorting and iterating a flat list of dirty nodes)
			// but this is the most fool proof for now.
			updateElementStyle(reg, DocumentUtil::getRootNode(reg), false);
			reg.clear<StyleDirtyTag>();
		}
	}

	void updateFonts(entt::registry& reg) {
		FontManager* fontManager = reg.ctx().at<FontManager*>();

		reg.view<styles::FontFamily, CurrentStyleDirtyTag>().each([&](StyleHandle style, const styles::FontFamily& family) {
			f32 size = reg.get<styles::FontSize>(style).value.value;
			reg.emplace<FontFaceStyle>(style, FontFaceStyle{ fontManager->loadFont(family.value.familyName, size) });
		});
	}

	void updateTransitions(entt::registry& reg, f32 dt) {
		reg.view<styles::TransitionProperty, CurrentStyleDirtyTag>().each([&](StyleHandle style, const styles::TransitionProperty& transition) {
			DomElementHandle element = reg.get<ElementReferenceComponent>(style).handle;
			StyleReferences& styleRef = reg.get<StyleReferences>(element);

			styleRef.from = styleRef.prev;
			styleRef.prev = entt::null;
			styleRef.to = StyleUtil::createEmptyStyle(reg, element);

			std::vector<std::string_view> propNames;
			splitString(transition.value, ' ', propNames);
			initializeTransitions(reg, element, styleRef, propNames);
		});

		reg.view<Transition>().each([&](const entt::entity e, Transition& transition) {
			assert(transition.state.duration > 0.0f);
			
			f32 frac;

			transition.state.pos += dt;
			bool finished = transition.state.pos >= transition.state.duration;

			if (!finished) {
				frac = transition.state.pos / transition.state.duration;
			} else {
				frac = 1.0f;
			}
			
			transition.func(reg, transition.from, transition.to, transition.target, frac);

			if (finished) {
				removeTransition(reg, transition.parent, e);
			}
		});
	}

	void cleanupStyles(entt::registry& reg) {
		reg.view<StyleUpdatedTag>().each([&](DomElementHandle element) {
			StyleReferences& styleRef = reg.get<StyleReferences>(element);
			if (styleRef.prev != entt::null) {
				reg.destroy(styleRef.prev);
				styleRef.prev = entt::null;
			}
		});

		reg.clear<StyleUpdatedTag, CurrentStyleDirtyTag>();
	}

	void StyleUtil::update(entt::registry& reg, f32 dt) {
		updateStyles(reg);
		updateFonts(reg);
		updateTransitions(reg, dt);
		cleanupStyles(reg);
	}

	void StyleUtil::setup(entt::registry& reg) {
		reg.ctx().emplace<StyleSingleton>(StyleSingleton{
			.propMethods = {
				makePropertyVTable<styles::Cursor>(),
				makePropertyVTable<styles::Color>(),
				makePropertyVTable<styles::BackgroundColor>(),

				makePropertyVTable<styles::MarginBottom>(),
				makePropertyVTable<styles::MarginTop>(),
				makePropertyVTable<styles::MarginLeft>(),
				makePropertyVTable<styles::MarginRight>(),

				makePropertyVTable<styles::PaddingBottom>(),
				makePropertyVTable<styles::PaddingTop>(),
				makePropertyVTable<styles::PaddingLeft>(),
				makePropertyVTable<styles::PaddingRight>(),

				makePropertyVTable<styles::BorderBottomWidth>(),
				makePropertyVTable<styles::BorderTopWidth>(),
				makePropertyVTable<styles::BorderLeftWidth>(),
				makePropertyVTable<styles::BorderRightWidth>(),
				makePropertyVTable<styles::BorderBottomColor>(),
				makePropertyVTable<styles::BorderTopColor>(),
				makePropertyVTable<styles::BorderLeftColor>(),
				makePropertyVTable<styles::BorderRightColor>(),
				makePropertyVTable<styles::BorderBottomStyle>(),
				makePropertyVTable<styles::BorderTopStyle>(),
				makePropertyVTable<styles::BorderLeftStyle>(),
				makePropertyVTable<styles::BorderRightStyle>(),

				// flex-flow
				makePropertyVTable<styles::FlexDirection>(),
				makePropertyVTable<styles::FlexWrap>(),

				makePropertyVTable<styles::FlexBasis>(),
				makePropertyVTable<styles::FlexGrow>(),
				makePropertyVTable<styles::FlexShrink>(),
				makePropertyVTable<styles::AlignItems>(),
				makePropertyVTable<styles::AlignContent>(),
				makePropertyVTable<styles::AlignSelf>(),
				makePropertyVTable<styles::JustifyContent>(),
				makePropertyVTable<styles::Overflow>(),

				makePropertyVTable<styles::Position>(),
				makePropertyVTable<styles::Top>(),
				makePropertyVTable<styles::Left>(),
				makePropertyVTable<styles::Bottom>(),
				makePropertyVTable<styles::Right>(),

				makePropertyVTable<styles::Width>(),
				makePropertyVTable<styles::Height>(),
				makePropertyVTable<styles::MinWidth>(),
				makePropertyVTable<styles::MinHeight>(),
				makePropertyVTable<styles::MaxWidth>(),
				makePropertyVTable<styles::MaxHeight>(),

				makePropertyVTable<styles::TransitionProperty>(),
				makePropertyVTable<styles::TransitionDuration>(),
				makePropertyVTable<styles::TransitionTimingFunction>(),
				makePropertyVTable<styles::TransitionDelay>(),
				makePropertyVTable<styles::FontFamily>(),
				makePropertyVTable<styles::FontSize>(),
				makePropertyVTable<styles::FontWeight>(),
				makePropertyVTable<styles::TextAlign>(),
			}
		});
	}
}
