#pragma once

#include <entt/entity/registry.hpp>

#include "DocumentTypes.h"
#include "DomStyle.h"
#include "graphics/FontManager.h"

namespace fw {
	class Document {
	private:
		entt::registry _reg;
		FontManager _fontManager;

	public:
		Document(FontManager& fontManager) : _fontManager(fontManager) { clear(); }
		~Document() = default;

		void clear();

		void loadStyle(const std::filesystem::path& path);

		DomElementHandle createElement(const std::string& tag);

		DomElementHandle createTextNode(const std::string& text);

		void appendChild(DomElementHandle node, DomElementHandle child);

		void removeChild(DomElementHandle node, DomElementHandle child, bool destroy);

		DomStyle getStyle(DomElementHandle element);

		entt::registry& getRegistry() {
			return _reg;
		}

		DomElementHandle getRootElement() const;

		void calculateLayout(DimensionF dim);

		void update(f32 dt);

		DomElementHandle getNodeHandle(YGNodeRef node) {
			return static_cast<DomElementHandle>(reinterpret_cast<std::uintptr_t>(YGNodeGetContext(node)));
		}

		template <typename Func>
		void each(DomElementHandle node, Func&& f) {
			YGNodeRef n = _reg.get<YGNodeRef>(node);
			uint32 childCount = YGNodeGetChildCount(n);

			for (uint32 i = 0; i < childCount; ++i) {
				f(getNodeHandle(YGNodeGetChild(n, i)));
			}
		}
	};
}
