#pragma once

#include <spdlog/spdlog.h>

#include "ui/View.h"
#include "GraphOverlay.h"
#include "NodeView.h"

#include "node/NodeGraph.h"

namespace fw {
	class GraphView : public View {
	private:
		struct Connection {
			NodeViewPtr outputNode;
			size_t outputIdx;
			NodeViewPtr inputNode;
			size_t inputIdx;
		};

		ViewPtr _container;
		GraphOverlayPtr _overlay;

		NodeGraphBasePtr _graph;

		std::vector<Connection> _connections;

	public:
		GraphView() {
			setType<GraphView>();

			_container = addChild<View>("Graph Container");
		}

		~GraphView() { }

		void setGraph(NodeGraphBasePtr graph) {
			_graph = graph;
			rebuildGraph();
		}

		bool onKey(VirtualKey::Enum key, bool down) override {
			return false;
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) final override {
			return false;
		}

		bool onMouseMove(Point pos) {
			return false;
		}

		void onRender(fw::Canvas& canvas) override {
			if (!_graph) {
				return;
			}

			//NVGcontext* vg = getVg();

			for (NodeGraphBase::Connection& conn : _graph->getConnections()) {
			}
		}

	private:
		void rebuildGraph() {
			_container->removeChildren();

			size_t i = 0;
			for (NodePtr node : _graph->getNodes()) {
				NodeViewPtr view = _overlay->addChild<NodeView>(fmt::format("Node {}", i));
				view->setNode(node);
			}
		}
	};
}
