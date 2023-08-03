#include "Document.h"

#include "graphics/FontUtil.h"
#include "ui/next/StyleComponents.h"
#include "graphics/Font.h"

#include "DocumentUtil.h"
#include <yoga/YGNode.h>

namespace fw {	
	YGNodeRef getNode(entt::registry& reg, entt::entity e) {
		return reg.get<YGNodeRef>(e);
	}

	void destroyYogaNode(entt::registry& reg, entt::entity e) {
		YGNodeFree(getNode(reg, e));
	}

	void setNodeHandle(YGNodeRef node, DomElementHandle handle) {
		YGNodeSetContext(node, reinterpret_cast<void*>(handle));
	}

	YGSize measureNode(YGNodeRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode, void* ctx) {
		entt::registry& reg = *static_cast<entt::registry*>(ctx);
		DomElementHandle handle = DocumentUtil::getNodeHandle(node);

		const TextComponent& text = reg.get<TextComponent>(handle);
		const FontFaceStyle* face = reg.try_get<FontFaceStyle>(handle);

		if (face && face->handle.isValid()) {
			DimensionF size = FontUtil::measureText(text.text, face->handle);
			return YGSize{ size.w, size.h };
		}

		return YGSize{ 0, 0 };
	}

	void Document::clear() {
		_reg = entt::registry();
		_reg.on_destroy<YGNodeRef>().connect<destroyYogaNode>();
		_root = createElement("div");
	}

	DomElementHandle createNode(entt::registry& reg) {
		DomElementHandle e = reg.create();
		YGNodeRef node = YGNodeNew();

		reg.emplace<YGNodeRef>(e, node);
		reg.emplace<FlexStyleFlag>(e, FlexStyleFlag::Empty);
		reg.emplace<EventFlag>(e, EventFlag::Empty);

		setNodeHandle(node, e);
		
		return e;
	}
	
	DomElementHandle Document::createElement(const std::string& tag) {
		DomElementHandle e = createNode(_reg);
		_reg.emplace<ElementTag>(e, ElementTag{ tag });
		return e;
	}
	
	DomElementHandle Document::createTextNode(const std::string& text) {
		DomElementHandle e = createNode(_reg);
		_reg.emplace<TextComponent>(e, TextComponent{ text });
		_reg.emplace<FontFaceStyle>(e, FontFaceStyle{ _fontManager.loadFont("Karla-Regular", 16) });
		getNode(_reg, e)->setMeasureFunc(measureNode);
		return e;
	}

	void Document::appendChild(DomElementHandle node, DomElementHandle child) {
		YGNodeRef nodeRef = getNode(_reg, node);
		YGNodeRef childRef = getNode(_reg, child);
		YGNodeInsertChild(nodeRef, childRef, YGNodeGetChildCount(nodeRef));
	}

	void Document::removeChild(DomElementHandle node, DomElementHandle child, bool destroy) {
		YGNodeRef nodeRef = getNode(_reg, node);
		YGNodeRef childRef = getNode(_reg, child);
		YGNodeRemoveChild(nodeRef, childRef);

		if (destroy) {
			// TODO: Recurse?
			_reg.destroy(child);
		}
	}
	
	DomStyle Document::getStyle(DomElementHandle element) {
		return DomStyle(_reg, element, getNode(_reg, element));
	}

	void updateWorldPositions(Document& doc, entt::entity e, const PointF& parent) {
		entt::registry& reg = doc.getRegistry();
		YGNodeRef node = getNode(reg, e);
		
		WorldAreaComponent& area = reg.emplace_or_replace<WorldAreaComponent>(e, WorldAreaComponent{
			.area = {
				YGNodeLayoutGetLeft(node) + parent.x,
				YGNodeLayoutGetTop(node) + parent.y,
				YGNodeLayoutGetWidth(node),
				YGNodeLayoutGetHeight(node)
			}
		});

		doc.each(e, [&](DomElementHandle child) {
			updateWorldPositions(doc, child, area.area.position);
		});
	}
	
	void Document::calculateLayout(DimensionF dim) {
		YGNodeCalculateLayoutWithContext(getNode(_reg, _root), dim.w, dim.h, YGDirectionInherit, &_reg);
		
		// Calculate world positions of all nodes
		updateWorldPositions(*this, _root, PointF());
	}
}
