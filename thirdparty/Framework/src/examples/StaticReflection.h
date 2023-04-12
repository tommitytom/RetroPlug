#pragma once

#include "application/Application.h"
#include "ui/PropertyInspector.h"
#include "ui/ObjectInspectorView.h"
#include "ui/ObjectInspectorUtil.h"
#include "ui/View.h"
#include "ui/NodeView.h"
#include "audio/AudioBuffer.h"
#include "foundation/node/NodeState.h"
#include "foundation/node/NodeGraphCompiler.h"
#include "foundation/node/NodeFactory.h"
#include "audio/nodes/SineOsc.h"

namespace fw {
	class SineAudioProcessor : public AudioProcessor {
	private:
		NodeGraphPtr _nodeGraph;
		f32 _phase = 0.0f;
		f32 _freq = 240.0f;
		f32 _amp = 0.5f;
		
		NodeProcessor _nodeProc;

	public:
		SineAudioProcessor(): _nodeProc(2048) {
			NodeFactory nodeFactory;
			nodeFactory.addNode<SineLfo, processSineLfo>();
			nodeFactory.addNode<SineOsc, processSine>();
			
			_nodeGraph = std::make_shared<NodeGraph>();
			auto lfoNode = _nodeGraph->addNode(nodeFactory.getNodeInfo<SineLfo>().allocNode());
			auto sineNode = _nodeGraph->addNode(nodeFactory.getNodeInfo<SineOsc>().allocNode());
			//_nodeGraph->connectNodes(lfoNode, 0, sineNode, 1);
			
			NodeGraphCompiler::build(*_nodeGraph, nodeFactory, _nodeProc);
		}

		~SineAudioProcessor() = default;

		void onRender(f32* output, const f32* input, uint32 frameCount) override {
			EventNode& ev = getEventNode();
			ev.update();

			NodeState<SineOsc>& node = _nodeProc.getNode<SineOsc>(1);

			node.frameCount = frameCount;
			auto& outputBuffer = node.output().output;
			outputBuffer.resize(frameCount);
			
			_nodeProc.process();

			StereoAudioBuffer out((StereoAudioBuffer::Frame*)output, frameCount, getSampleRate());
			out.clear();

			for (uint32 i = 0; i < frameCount; ++i) {
				f32 sample = outputBuffer.getSample(i, 0);
				//out.setSample(i, 0, sample);
				//out.setSample(i, 1, sample);
			}
		}

		f32 getSampleRate() const {
			return 48000.0f;
		}
	};
	
	class StaticReflection : public View {
	private:
		ObjectInspectorViewPtr _objectInspector;

		NodeGraphPtr _graph;

	public:
		StaticReflection() : View({ 1024, 768 }) {
			setType<StaticReflection>();
			setSizingPolicy(SizingPolicy::FitToParent);
			setFocusPolicy(FocusPolicy::Click);
		}

		~StaticReflection() = default;

		void rebuildPropertyGrid() {
			removeChildren();

			_objectInspector = addChildAt<ObjectInspectorView>("Property Editor", { 50, 50 });
			_objectInspector->setSizingPolicy(SizingPolicy::None);
			_objectInspector->setDimensions({ 300, 600 });

			_graph = std::make_shared<NodeGraph>();
			auto node1 = _graph->addNode<SineOsc>();
			node1->setPosition({ 100, 100 });
			auto node2 = _graph->addNode<SineOsc>();
			node2->setPosition({ 500, 100 });

			//_graph->connectNodes(node1, 2, node2, 0);

			NodeGraphViewPtr nodeView = addChild<NodeGraphView>("Node Graph");
			nodeView->setSizingPolicy(fw::SizingPolicy::FitToParent);
			nodeView->setGraph(_graph);
			
			subscribe<NodeSelectionChanged>(nodeView, [](const NodeSelectionChanged& ev) {
				//_objectInspector
			});
		}

		void onInitialize() override {
			rebuildPropertyGrid();
		}

		void onHotReload() override {
			spdlog::info("hot reload!");
			rebuildPropertyGrid();
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			return false;
		}

		bool onKey(const KeyEvent& ev) override {
			return false;
		}

		void onUpdate(f32 delta) override {

		}

		void onRender(fw::Canvas& canvas) override {
			canvas.fillRect(getDimensionsF(), Color4F::blue);
		}
	};

	using StaticReflectionApplication = fw::app::BasicApplication<StaticReflection, SineAudioProcessor>;
}
