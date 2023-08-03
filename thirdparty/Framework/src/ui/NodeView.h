#pragma once

#include "ui/View.h"
#include "foundation/node/NodeGraph.h"

namespace fw {	
	const int32 PORT_HEIGHT = 10;
	const int32 TITLE_HEIGHT = 20;
	const int32 PORT_SPACING = 30;

	enum class PortDirection {
		Input,
		Output
	};

	class NodePortView : public View {
	private:
		uint32 _type = 0;

		Point _clickPos;
		bool _mouseDown = false;
		PortDirection _direction = PortDirection::Input;
		bool _mouseOver = false;

	public:
		NodePortView() {
			setType<NodePortView>();
			setFocusPolicy(fw::FocusPolicy::Click);
		}

		void setPortType(uint32 type, PortDirection direction) {
			_type = type;
			_direction = direction;
		}

		uint32 getPortType() const {
			return _type;
		}

		PortDirection getPortDirection() const {
			return _direction;
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			_clickPos = position;
			_mouseDown = down;
			return true;
		}

		void onMouseEnter(Point pos) override {
			_mouseOver = true;
		}

		void onMouseLeave() override {
			_mouseOver = false;
		}

		bool onMouseMove(Point position) override {
			if (_mouseDown) {
				int32 dist = (position - _clickPos).magnitude();

				if (dist > 10) {
					beginDrag(nullptr, position);
					_mouseDown = false;
				}
				
				return true;
			}

			return false;
		}

		Rect getPortArea() const {
			Rect area = getArea();
			area.w = PORT_HEIGHT;
			area.h = PORT_HEIGHT;
			
			if (_direction == PortDirection::Output) {
				area.position.x = getArea().right() - PORT_HEIGHT;
			}
			
			return area;
		}

		Rect getPortAreaLocal() const {
			Rect area;
			area.w = PORT_HEIGHT;
			area.h = PORT_HEIGHT;

			if (_direction == PortDirection::Output) {
				area.position.x = getArea().w - PORT_HEIGHT;
			}

			return area;
		}

		void onRender(fw::Canvas& canvas) override {
			Rect portArea = getPortAreaLocal();
			Rect portHandle = portArea.shrink(2);
			
			if (_direction == PortDirection::Input) {
				canvas.text((PointF)portArea.topRight(), getName(), fw::Color4F::black);
				portHandle.x = 0;
			} else {
				PointF pos = (PointF)portArea.position;
				pos.x -= canvas.measureText(getName()).w;
				canvas.text(pos, getName(), fw::Color4F::black);
				portHandle.x = getDimensions().w - portHandle.w;
			}

			fw::Color4F handleColor = fw::Color4F::black;
			if (_mouseOver) {
				handleColor = fw::Color4F::red;
			}

			canvas.fillRect(portHandle, handleColor);
		}
	};

	using NodePortViewPtr = std::shared_ptr<NodePortView>;
	
	class NodeView : public View {
	private:
		NodePtr _node;

		std::vector<NodePortViewPtr> _inputPorts;
		std::vector<NodePortViewPtr> _outputPorts;

		Point _clickPos;
		bool _mouseDown = false;

	public:
		NodeView() { 
			setType<NodeView>();
			setFocusPolicy(fw::FocusPolicy::Click);
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			_clickPos = position;
			_mouseDown = down;
			return true;
		}

		bool onMouseMove(Point position) override {
			if (_mouseDown) {
				int32 dist = (position - _clickPos).magnitude();

				if (dist > 10) {
					beginDrag(nullptr, position);
					_mouseDown = false;
				}

				return true;
			}

			return false;
		}

		Rect getInputPortArea(size_t index) const {
			Rect portArea = _inputPorts[index]->getPortArea();
			portArea.position += getPosition();
			return portArea;
		}

		Rect getOutputPortArea(size_t index) const {
			Rect portArea = _outputPorts[index]->getPortArea();
			portArea.position += getPosition();
			return portArea;
		}

		Point getInputPortConnectionPoint(size_t index) const {
			Rect portArea = getInputPortArea(index);
			portArea.position.y += portArea.h / 2;
			return portArea.position;
		}

		Point getOutputPortConnectionPoint(size_t index) const {
			Rect portArea = getOutputPortArea(index);
			portArea.position.x += portArea.w;
			portArea.position.y += portArea.h / 2;
			return portArea.position;
		}

		Point getOutputPortConnectionPoint(NodePortViewPtr port) const {
			for (size_t i = 0; i < _outputPorts.size(); ++i) {
				if (_outputPorts[i] == port) {
					return getOutputPortConnectionPoint(i);
				}
			}

			assert(false);
			return Point();
		}

		Point getInputPortConnectionPoint(NodePortViewPtr port) const {
			for (size_t i = 0; i < _inputPorts.size(); ++i) {
				if (_inputPorts[i] == port) {
					return getInputPortConnectionPoint(i);
				}
			}

			assert(false);
			return Point();
		}

		void setNode(NodePtr node) {
			_node = node;

			_inputPorts.clear();
			_outputPorts.clear();
			removeChildren();

			for (auto& input : _node->getInputs()) {
				auto inputView = this->addChild<NodePortView>(input.name);
				inputView->setPortType(input.type, PortDirection::Input);
				_inputPorts.push_back(inputView);
			}

			for (auto& output : _node->getOutputs()) {
				auto outputView = this->addChild<NodePortView>(output.name);
				outputView->setPortType(output.type, PortDirection::Output);
				_outputPorts.push_back(outputView);
			}

			updateLayout();
		}

		void updateLayout() {
			int32 yOffset = 30;

			for (auto& input : _inputPorts) {
				input->setArea(Rect{ 0, yOffset, 100, 20 });
				yOffset += 30;
			}

			yOffset = 30;

			for (auto& output : _outputPorts) {
				output->setArea(Rect{ getDimensions().w - 100, yOffset, 100, 20 });
				yOffset += 30;
			}
		}

		void onRender(fw::Canvas& canvas) override {
			auto dim = getDimensionsF();

			canvas.fillRect(dim, Color4F::lightGrey);
			dim.h = 20;
			canvas.fillRect(dim, Color4F(0.55f, 0.55f, 0.55f, 1.0f));

			PointF nodePos(0, 2.0f);
			canvas.setTextAlign(fw::TextAlignFlags::Top);
			canvas.text(nodePos, _node->getName(), fw::Color4F::black);
		}

		uint32 getNodeIndex() const {
			return _node->getIndex();
		}
	};

	using NodeViewPtr = std::shared_ptr<NodeView>;

	enum class GraphDragState {
		None,
		Held,
		Node,
		Port
	};

	struct NodeSelectionChanged {
		NodeViewPtr selected;
	};

	class NodeGraphView : public View {
	private:
		NodeGraphPtr _graph;
		Point _clickPos;
		bool _mouseDown = false;

		GraphDragState _dragState = GraphDragState::None;
		NodeViewPtr _movingNode;

		std::vector<NodeViewPtr> _nodes;
		uint32 _selectedIndex = -1;

		std::optional<std::pair<Point, Point>> _draggingLine;
		//Point _mousePos;

	public:
		NodeGraphView() { setType<NodeGraphView>(); }
		NodeGraphView(Dimension dimensions) : View(dimensions) { setType<NodeGraphView>(); }
		~NodeGraphView() { }

		void setGraph(NodeGraphPtr graph) {
			_graph = graph;
			updateLayout();
		}

		void updateLayout() {
			_nodes.clear();
			removeChildren();			

			if (_graph) {
				for (auto node : _graph->getNodes()) {
					NodeViewPtr nodeView = this->addChildAt<NodeView>(node->getName(), node->getPosition());
					nodeView->getLayout().setDimensions(Dimension{ 200, 200 });
					nodeView->setNode(node);
					_nodes.push_back(nodeView);

					subscribe<MouseButtonEvent>(nodeView, [this, nodeView](const MouseButtonEvent& ev) {
						if (ev.button == MouseButton::Left && ev.down) {
							_selectedIndex = nodeView->getNodeIndex();
							emit(NodeSelectionChanged{ nodeView });
						}
					});
				}
			}
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			_clickPos = position;
			_mouseDown = down;

			if (down) {
				_dragState = GraphDragState::Held;
				
				if (_selectedIndex != -1) {
					_selectedIndex = -1;
					emit(NodeSelectionChanged{});
				}
			} else {
				_dragState = GraphDragState::None;
			}
			
			clearSelection();

			return true;
		}

		void clearSelection() {
			_selectedIndex = -1;
		}

		bool onMouseMove(Point position) override {
			//_mousePos = position;
			/*if (_movingNode) {
				_movingNode->setPosition(position);
			}

			spdlog::info("Move over graph");

			switch (_dragState) {
				case GraphDragState::Held: {
					int32 dist = (position - _clickPos).magnitude();

					if (dist > 10) {
						// Start drag
						for (auto child : getChildren()) {
							if (child->getArea().contains(position) && child->isType<NodeView>()) {
								_dragState = GraphDragState::Node;
								_movingNode = child->asShared<NodeView>();
								break;
							}
						}

						if (_dragState == GraphDragState::Held) {
							// Not over a node, drag draph
							_dragState = GraphDragState::None;
						}
					}

					break;
				}
			}*/

			return true;
		}
		
		bool onDragMove(DragContext& ctx, Point pos) override {
			if (ctx.source->isType<NodeView>()) {
				Point newPos = pos - ctx.sourcePoint;
				ctx.source->getLayout().setPositionEdge(FlexEdge::Left, newPos.x);
				ctx.source->getLayout().setPositionEdge(FlexEdge::Top, newPos.y);
			}

			if (ctx.source->isType<NodePortView>()) {
				auto portView = ctx.source->asShared<NodePortView>();
				auto nodeView = portView->getParent()->asShared<NodeView>();
				
				Point startPoint;
				if (portView->getPortDirection() == PortDirection::Input) {
					startPoint = nodeView->getInputPortConnectionPoint(portView);
				} else {
					startPoint = nodeView->getOutputPortConnectionPoint(portView);
				}
				
				Point endPoint = pos;

				for (auto target : ctx.targets) {
					if (target->isType<NodePortView>()) {
						auto targetPortView = target->asShared<NodePortView>();
						
						if (targetPortView->getPortDirection() != portView->getPortDirection() && 
							targetPortView->getParent() != portView->getParent() &&
							targetPortView->getPortType() == portView->getPortType()
						) {
							auto targetNodeView = targetPortView->getParent()->asShared<NodeView>();

							if (targetPortView->getPortDirection() == PortDirection::Input) {
								endPoint = targetNodeView->getInputPortConnectionPoint(targetPortView);
							} else {
								endPoint = targetNodeView->getOutputPortConnectionPoint(targetPortView);
							}

							break;
						}
					}
				}

				_draggingLine = std::pair<Point, Point>(startPoint, endPoint);
			}

			return true;
		}

		bool onDrop(DragContext& ctx, Point position) override { 
			_draggingLine.reset();
			return true; 
		}

		void onRender(fw::Canvas& canvas) override {
			canvas.setFont("Karla-Regular", 11.75f);
			canvas.fillRect(getDimensionsF(), Color4F::darkGrey);

			if (!_graph) {
				return;
			}

			for (auto& conn : _graph->getConnections()) {
				auto inputNode = conn.inputNode.lock();
				auto outputNode = conn.outputNode.lock();
				assert(inputNode);
				assert(outputNode);

				auto inputNodeView = _nodes[inputNode->getIndex()];
				auto outputNodeView = _nodes[outputNode->getIndex()];

				auto inputArea = inputNodeView->getInputPortArea(conn.inputIdx);
				auto outputArea = outputNodeView->getOutputPortArea(conn.outputIdx);

				Point startPoint(outputArea.right(), outputArea.y + outputArea.h / 2);
				Point endPoint(inputArea.x, inputArea.y + inputArea.h / 2);

				canvas.line(startPoint, endPoint, fw::Color4F::white);

				//assert(input.type == output.type);
			}

			if (_selectedIndex != -1) {
				auto selected = _nodes[_selectedIndex];
				auto highlightArea = selected->getArea().grow(3);
				canvas.strokeRect(RectF(highlightArea), fw::Color4F::green);
			}

			if (_draggingLine.has_value()) {
				canvas.line(_draggingLine.value().first, _draggingLine.value().second, fw::Color4F::white);
			}

			for (int32 y = 0; y < getDimensions().h; ++y) {
				for (int32 x = 0; x < getDimensions().w; ++x) {

					//canvas.setPixel(x, y, Color4F::lightGrey);
				}
			}
		}
	};
	
	using NodeGraphViewPtr = std::shared_ptr<NodeGraphView>;
}
