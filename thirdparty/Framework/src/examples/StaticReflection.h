#pragma once

#include <simdjson/simdjson.h>
#include <httplib.h>

#include "application/Application.h"
#include "ui/PropertyInspector.h"
#include "ui/ObjectInspectorView.h"
#include "ui/ObjectInspectorUtil.h"
#include "ui/View.h"
#include "ui/NodeView.h"
#include "ui/VerticalSplitter.h"
#include "audio/AudioBuffer.h"
#include "foundation/node/NodeState.h"
#include "foundation/node/NodeGraphCompiler.h"
#include "foundation/node/NodeFactory.h"
#include "foundation/node/NodeGraph.h"
#include "foundation/node/Node.h"
#include "foundation/node/NodeRegistry.h"
#include "audio/nodes/SineOsc.h"

namespace fw {
	using SubscriptionId = uint32;
	
	struct NodeSubscribeEvent {
		SubscriptionId subscriptionId = 0;
		uint32 nodeId = 0;
	};

	struct NodeUnsubscribeEvent {
		SubscriptionId subscriptionId = 0;
	};

	struct SetGraphEvent {
		NodeGraphPtr graph;
	};

	struct SetGraphProcessorEvent {
		std::unique_ptr<NodeProcessor> processor;
	};

	struct AcquireGraphEvent {
		NodeGraphPtr graph;
	};

	struct SubscriptionData {
		SubscriptionId id = 0;
		entt::any data;
	};
	
	class SineAudioProcessor : public AudioProcessor {
	private:
		struct Subscription {
			uint32 id = 0;
			uint32 nodeId = 0;
		};
		
		NodeGraphPtr _nodeGraph;		
		NodeFactory _nodeFactory;
		std::unique_ptr<NodeProcessor> _nodeProc;
		std::vector<Subscription> _subscriptions;

		fw::NodeRegistry _nodeRegistry;

	public:
		SineAudioProcessor() {
			/*_nodeRegistry.addNode(entt::type_hash<SineOsc>::value()).addLayer<AudioLayer, SineOsc, processSine>();
			_nodeRegistry.addNode(entt::type_hash<ConstFloatNode>::value())
				.addLayer<AudioLayer, ConstFloatNode, processConstFloatNode>()
				.addLayer<VisualLayer, ConstFloatNode, processConstFloatNode>();*/

			_nodeFactory.addNode<SineLfo, processSineLfo>();
			_nodeFactory.addNode<SineOsc, processSine>();
			_nodeFactory.addNode<ConstFloatNode, processConstFloatNode>();
			_nodeFactory.addNode<AddFloatNode, processAddFloat>();
			
			_nodeGraph = std::make_shared<NodeGraph>();
			auto constNode = _nodeGraph->addNode(_nodeFactory.getNodeInfo<ConstFloatNode>().allocNode());
			auto lfoNode = _nodeGraph->addNode(_nodeFactory.getNodeInfo<SineLfo>().allocNode());
			auto sineNode = _nodeGraph->addNode(_nodeFactory.getNodeInfo<SineOsc>().allocNode());
			_nodeGraph->connectNodes(constNode, 0, sineNode, 1);

			constNode->setPosition({ 10, 100 });
			lfoNode->setPosition({ 10, 400 });
			sineNode->setPosition({ 300, 100 });
			
			_nodeProc = std::make_unique<NodeProcessor>(2048);
			NodeGraphCompiler::build(*_nodeGraph, _nodeFactory, *_nodeProc);

			getEventNode().receive<SetGraphProcessorEvent>([this](SetGraphProcessorEvent&& ev) {
				_nodeProc = std::move(ev.processor);
			});

			getEventNode().receive<AcquireGraphEvent>([this](AcquireGraphEvent&& ev) {
				getEventNode().send("Ui"_hs, AcquireGraphEvent{ std::move(_nodeGraph) });
			});

			getEventNode().receive<NodeSubscribeEvent>([this](NodeSubscribeEvent&& ev) {
				assert(ev.nodeId != -1);
				
				spdlog::info("Subscription {} added", ev.subscriptionId);

				_subscriptions.push_back(Subscription { 
					.id = ev.subscriptionId, 
					.nodeId = ev.nodeId 
				});
			});

			getEventNode().receive<NodeUnsubscribeEvent>([this](NodeUnsubscribeEvent&& ev) {
				spdlog::info("Subscription {} removed", ev.subscriptionId);
				
				for (auto it = _subscriptions.begin(); it != _subscriptions.end(); ++it) {
					if (it->id == ev.subscriptionId) {
						_subscriptions.erase(it);
						break;
					}
				}
			});
		}

		~SineAudioProcessor() = default;

		void onRender(f32* output, const f32* input, uint32 frameCount) override {
			EventNode& ev = getEventNode();
			ev.update();

			NodeState<SineOsc>& node = _nodeProc->getNode<SineOsc>(2);

			node.frameCount = frameCount;
			auto& outputBuffer = node.output().output;
			outputBuffer.resize(frameCount);
			
			_nodeProc->process();

			StereoAudioBuffer out((StereoAudioBuffer::Frame*)output, frameCount, getSampleRate());
			out.clear();

			for (uint32 i = 0; i < frameCount; ++i) {
				f32 sample = outputBuffer.getSample(i, 0);
				out.setSample(i, 0, sample);
				out.setSample(i, 1, sample);
			}

			for (auto& sub : _subscriptions) {
				assert(sub.nodeId != -1);

				ev.trySend("Ui"_hs, SubscriptionData {
					.id = sub.id,
					.data = std::move(_nodeProc->getNodeState(sub.nodeId))
				});
			}
		}

		f32 getSampleRate() const {
			return 48000.0f;
		}
	};

	struct NodeInspectorDesc {
		std::function<void(ObjectInspectorViewPtr, entt::any)> inspect;
	};
		
	class StaticReflection : public View {
	private:
		NodeGraphPtr _graph;
		
		NodeGraphViewPtr _graphView;
		ObjectInspectorViewPtr _objectInspector;

		uint32 _nextSubscriptionIndex = 1;
		uint32 _selectedNode = -1;

		uint32 _subscriberIndex = -1;
		entt::any _subData;

		//std::unordered_map<SubscriptionId, >
		
		FullNodeState<SineOsc> _testState;

		std::unordered_map<entt::id_type, NodeInspectorDesc> _inspectors;

		NodeRegistry _nodeRegistry;

	public:
		StaticReflection() : View({ 1024, 768 }) {
			setType<StaticReflection>();
			setSizingPolicy(SizingPolicy::FitToParent);
			setFocusPolicy(FocusPolicy::Click);

			this->addInspector<SineOsc>();
			this->addInspector<SineLfo>();
			this->addInspector<ConstFloatNode>();
		}

		~StaticReflection() = default;

		template <typename T>
		void addInspector() {
			_inspectors[entt::type_hash<FullNodeState<T>>::value()] = NodeInspectorDesc{
				.inspect = [this](ObjectInspectorViewPtr inspector, entt::any nodeState) {
					inspector->clear();
					
					FullNodeState<T>& state = entt::any_cast<FullNodeState<T>&>(nodeState);

					std::string_view name = get_display_name(refl::reflect<T>());
					
					if constexpr (refl::member_list<typename T::Input>::size > 0) {
						inspector->pushGroup(fmt::format("{} - Input", name));
						ObjectInspectorUtil::reflect(inspector, state.input);
					}
					
					if constexpr (refl::member_list<typename T::Output>::size > 0) {
						inspector->pushGroup(fmt::format("{} - Output", name));
						ObjectInspectorUtil::reflect(inspector, state.output);
					}
					
					if constexpr (refl::member_list<T>::size > 0) {
						inspector->pushGroup(fmt::format("{} - State", name));
						ObjectInspectorUtil::reflect(inspector, state.state);
					}

					inspector->updateLayout();
				}
			};
		}

		void onInitialize() override {
			fw::EventNode& eventNode = getState<fw::EventNode>();

			auto splitter = addChild<DockSplitter>("Splitter");
			splitter->setSizingPolicy(fw::SizingPolicy::FitToParent);			

			//_graphView = addChild<NodeGraphView>("Node Graph");
			_graphView = splitter->addItem<NodeGraphView>("Node Graph", 300);
			_graphView->setSizingPolicy(fw::SizingPolicy::FitToParent);
			_graphView->setGraph(_graph);

			//_objectInspector = addChildAt<ObjectInspectorView>("Property Editor", { 10, 10 });
			_objectInspector = splitter->addItem<ObjectInspectorView>("Node Graph", 300);
			_objectInspector->setSizingPolicy(fw::SizingPolicy::FitToParent);
			//_objectInspector->setSizingPolicy(SizingPolicy::None);
			//_objectInspector->setDimensions({ 300, 600 });

			_inspectors[entt::type_hash<FullNodeState<SineOsc>>::value()].inspect(_objectInspector, entt::forward_as_any(_testState));

			subscribe<NodeSelectionChanged>(_graphView, [this](const NodeSelectionChanged& ev) {
				if (_selectedNode != -1) {
					getState<fw::EventNode>().send("Audio"_hs, NodeUnsubscribeEvent{ _subscriberIndex });
					_subscriberIndex = -1;
				}

				_selectedNode = ev.selected ? ev.selected->getNodeIndex() : -1;
				
				if (_selectedNode != -1) {
					uint32 subIndex = _nextSubscriptionIndex++;
					_subscriberIndex = subIndex;
					getState<fw::EventNode>().send("Audio"_hs, NodeSubscribeEvent{ subIndex, _selectedNode });
				}
			});
			
			eventNode.receive<AcquireGraphEvent>([this](AcquireGraphEvent&& ev) {
				_graph = std::move(ev.graph);
				_graphView->setGraph(_graph);
			});

			eventNode.receive<SubscriptionData>([&](SubscriptionData&& sub) {
				entt::id_type type = sub.data.type().hash();
				
				auto found = _inspectors.find(type);

				if (found != _inspectors.end()) {
					_subData = std::move(sub.data);
					assert(_subData.owner());
					found->second.inspect(_objectInspector, _subData);
				} else {
					spdlog::error("No inspector for type {}", type);
				}				
			});

			eventNode.send("Audio"_hs, AcquireGraphEvent{});
		}

		void onHotReload() override {
			spdlog::info("hot reload!");
			_graphView->setGraph(_graph);
		}

		void onUpdate(f32 delta) override {
			getState<fw::EventNode>().update();
		}

		void onRender(fw::Canvas& canvas) override {
			canvas.fillRect(getDimensionsF(), Color4F::blue);
		}
	};

	using StaticReflectionApplication = fw::app::BasicApplication<StaticReflection, SineAudioProcessor>;
}
