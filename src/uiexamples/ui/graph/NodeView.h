#pragma once

#include <nanovg.h>

#include "ui/View.h"
#include "node/NodeBase.h"

namespace rp {
	class NodeView : public View {
	private:
		NodePtr _node;

	public:
		NodeView() { setType<NodeView>(); }
		NodeView(Dimension dimensions) : View(dimensions) { setType<NodeView>(); }
		~NodeView() { }

		void setNode(NodePtr node) {
			_node = node;
			setPosition(node->getPosition());
		}

		void onRender() override {
			NVGcontext* vg = getVg();
			auto area = getArea();

			nvgBeginPath(vg);
			nvgRect(vg, (f32)area.x + 0.5f, (f32)area.y, (f32)area.w - 0.5f, (f32)area.h - 0.5f);
			nvgStrokeWidth(vg, 0.5f);
			nvgStrokeColor(vg, nvgRGBA(255, 0, 0, 220));
			nvgStroke(vg);
		}
	};

	using NodeViewPtr = std::shared_ptr<NodeView>;
}
