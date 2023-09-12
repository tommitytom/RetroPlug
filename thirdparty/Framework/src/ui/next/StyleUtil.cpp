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

	void StyleUtil::addStyleSheets(entt::registry& reg, std::vector<Stylesheet>&& stylesheets) {
		StyleSingleton& styleSingleton = reg.ctx().at<StyleSingleton>();
		styleSingleton.selectors.insert(styleSingleton.selectors.end(), stylesheets.begin(), stylesheets.end());

		// Force all nodes to update their styles
		reg.view<StyleReferences>().each([&](entt::entity e) { reg.emplace_or_replace<StyleDirtyTag>(e); });
	}

	void StyleUtil::setup(entt::registry& reg) {
		reg.ctx().emplace<StyleSingleton>(StyleSingleton{
			.propMethods = {
				makePropertyVTable<styles::BackgroundColor>(),
				makePropertyVTable<styles::Color>(),
				makePropertyVTable<styles::Height>(),
				makePropertyVTable<styles::TransitionDelay>(),
				makePropertyVTable<styles::TransitionDuration>(),
				makePropertyVTable<styles::TransitionProperty>(),
				makePropertyVTable<styles::TransitionTimingFunction>(),
				makePropertyVTable<styles::FontFamily>(),
				makePropertyVTable<styles::FontSize>(),
				makePropertyVTable<styles::FontWeight>(),
				makePropertyVTable<styles::TextAlign>(),
				makePropertyVTable<styles::Width>()
			}
		});
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
	const T* findParentProp(entt::registry& reg, const DomElementHandle handle) {
		DomElementHandle parent = DocumentUtil::getParent(reg, handle);

		while (parent != entt::null) {
			const T* found = reg.try_get<T>(reg.get<StyleReferences>(handle).current);
			
			if (found) {
				return found;
			}

			return nullptr;
		}

		return nullptr;
	}

	template <typename T>
	void inheritProperty(entt::registry& reg, const DomElementHandle sourceElement, const StyleHandle targetStyle) {
		const T* prop = findParentProp<T>(reg, sourceElement);
		if (prop) {
			reg.emplace<T>(targetStyle, *prop);
		}
	}

	StyleHandle createTextStyle(entt::registry& reg, const DomElementHandle handle) {
		StyleHandle style = StyleUtil::createEmptyStyle(reg, handle);
		
		inheritProperty<styles::FontFamily>(reg, handle, style);
		inheritProperty<styles::FontSize>(reg, handle, style);
		inheritProperty<styles::FontWeight>(reg, handle, style);

		return style;
	}

	StyleHandle createStyle(entt::registry& reg, const DomElementHandle handle) {
		const TextComponent* text = reg.try_get<TextComponent>(handle);
		if (text) {
			return createTextStyle(reg, handle);
		}

		StyleReferences& styleRef = reg.get<StyleReferences>(handle);
		const StyleSingleton& styleSingleton = reg.ctx().at<StyleSingleton>();
		std::vector<const StylesheetRule*> rules;

		for (const Stylesheet& selector : styleSingleton.selectors) {
			if (isSelected(reg, handle, selector.selectorGroup)) {
				rules.push_back(selector.rule.get());
			}
		}

		// TODO: Sort and filter by specificity

		std::unordered_map<entt::id_type, const StylesheetRule::Property*> props;

		for (const StylesheetRule* rule : rules) {
			for (const StylesheetRule::Property& prop : rule->properties) {
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

				spdlog::info(reg.get<ElementTag>(element).tag);

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

	void updateElementStyle(entt::registry& reg, DomElementHandle elementHandle, bool force) {
		if (reg.all_of<StyleDirtyTag>(elementHandle) || force) {
			assert(reg.all_of<StyleReferences>(elementHandle));
			StyleReferences& styleRef = reg.get<StyleReferences>(elementHandle);
			
			while (styleRef.transitions.size()) {
				removeTransition(reg, elementHandle, styleRef.transitions.back());
			}

			assert(styleRef.from == entt::null);
			assert(styleRef.to == entt::null);

			StyleHandle nextStyle = createStyle(reg, elementHandle);
			reg.emplace<LayoutDirtyTag>(nextStyle);
			reg.emplace<CurrentStyleTag>(nextStyle);
			reg.emplace<CurrentStyleDirtyTag>(nextStyle);

			// this should probably be deferred to transitionSystem
			styles::TransitionProperty* transition = reg.try_get<styles::TransitionProperty>(nextStyle);
			if (transition && reg.all_of<styles::TransitionDuration>(nextStyle)) {
				styleRef.from = styleRef.current;
				//reg.emplace<PreviousStyleTag>(styleRef.from);
				reg.remove<CurrentStyleTag, CurrentStyleDirtyTag>(styleRef.from);

				styleRef.current = nextStyle;
				styleRef.to = StyleUtil::createEmptyStyle(reg, elementHandle);

				std::vector<std::string_view> propNames;
				splitString(transition->value, ' ', propNames);
				initializeTransitions(reg, elementHandle, styleRef, propNames);
			} else {
				styleRef.current = nextStyle;
			}

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
			updateElementStyle(reg, DocumentUtil::getRootNode(reg), false);
			reg.clear<StyleDirtyTag>();
		}
	}

	void updateTransitions(entt::registry& reg, f32 dt) {
		reg.view<styles::TransitionProperty, CurrentStyleDirtyTag>().each([&](StyleHandle style, const styles::TransitionProperty& transition) {
			// get style ref
			// remove existing transitions

			
			/*reg.emplace<PreviousStyleTag>(styleRef.from);
			reg.remove<CurrentStyleTag, CurrentStyleDirtyTag>(styleRef.from);

			styleRef.current = nextStyle;
			styleRef.to = StyleUtil::createEmptyStyle(reg, elementHandle);

			std::vector<std::string_view> propNames;
			splitString(transition.value, ' ', propNames);
			initializeTransitions(reg, elementHandle, styleRef, propNames);*/
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

	void StyleUtil::update(entt::registry& reg, f32 dt) {
		updateStyles(reg);
		updateTransitions(reg, dt);
	}
}
