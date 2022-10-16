#include "UiDocking.h"

#include <future>

#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"

#include "ui/DockWindow.h"
#include "ui/TabView.h"
#include "ui/Colors.h"
#include "ui/DockTabView.h"
#include "ui/WaveView.h"
#include "ui/WaveformUtil.h"

#include "engine/EngineModule.h"
#include "engine/SceneEditorView.h"
#include "ui/ObjectInspectorView.h"

#include "audio/AudioLoaderUtil.h"

#include "examples/Whitney.h"

//#include "core/RetroPlugNodes.h"

//#include "node/NodeGraph.h"
//#include "node/AudioGraph.h"

using namespace fw;

/*void testNodeGraph() {
	NodeGraph<AudioGraphProcessor> nodeGraph;
	std::shared_ptr<AudioGraphProcessor> processor = nodeGraph.createProcessor();

	AudioBuffer b1(1024);
	b1.clear(1.0f);

	AudioBuffer b2(1024);
	b2.clear(2.0f);

	auto bn1 = nodeGraph.addNode<AudioBufferNode>();
	auto bn2 = nodeGraph.addNode<AudioBufferNode>();
	auto adder = nodeGraph.addNode<AddNode>();

	bn1->setBuffer(std::move(b1));
	bn2->setBuffer(std::move(b2));

	nodeGraph.connectNodes(bn1, 0, adder, 0);
	nodeGraph.connectNodes(bn2, 0, adder, 1);

	processor->onProcess();
	nodeGraph.onProcess();
}*/

class DraggablePanel : public PanelView {
private:
	Point _clickPos;
	bool _mouseDown = false;

public:
	DraggablePanel() {
		setType<DraggablePanel>();
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
				beginDrag(nullptr);
			}
		}

		return true;
	}

	void onDragFinish(DragContext& ctx) {
		_mouseDown = false;
	}
};

class DropZone : public PanelView {
public:
	DropZone() {
		setType<DropZone>();
	}

	void onDragEnter(DragContext& ctx, Point position) override {}

	bool onDragMove(DragContext& ctx, Point position) override {
		ctx.source->setPosition(position);
		return true;
	}

	void onDragLeave(DragContext& ctx) override {}

	bool onDrop(DragContext& ctx, Point position) {
		spdlog::info("drop catch!");
		return true;
	}
};

#include "foundation/Event.h"

UiDocking::UiDocking() : View({ 1500, 1000 }) {
	EngineModule::setup();
	
	setType<UiDocking>();
	setName("Example Application");
	setSizingPolicy(SizingPolicy::FitToParent);
}

bool isApproximately(f32 v, f32 target, f32 epsilon) {
	return v >= target - epsilon && v <= target + epsilon;
}

using EntityReference = std::pair<entt::registry*, entt::entity>;

void UiDocking::onUpdate(f32 delta) {
	if (!_propGrid->getChildren().size()) {
		SelectionSingleton& state = *getState<SelectionSingleton>();

		if (state.selected.size()) {
			entt::meta_type type = state.selected[0].type();

			if (type == entt::resolve<EntityReference>()) {
				EntityReference ref = state.selected[0].cast<EntityReference>();

				EngineUtil::visitComponents(*ref.first, ref.second, [&](const entt::type_info info) {
					entt::meta_type type = entt::resolve(info);
					
					if (type) {

					} else {
						// Just print the name
					}
				});
			} else {
				_propGrid->addObject("Test", state.selected[0].as_ref());
			}
		}
	}
}

void UiDocking::onInitialize() {
	auto rootPanel = addChild<Dock>("Root Panel");
	//rootPanel->setDimensions({ 800, 600 });
	rootPanel->setSizingPolicy(SizingPolicy::FitToParent);
	//rootPanel->setColor(COLOR_WHITE);

	auto dockRoot = std::make_shared<DockSplitter>();
	dockRoot->setName("Vertical Split");
	dockRoot->setSplitDirection(SplitDirection::Vertical);
	dockRoot->setSizingPolicy(SizingPolicy::FitToParent);
	//dockRoot->setArea({ 100, 100, 400, 200 });

	rootPanel->setRoot(dockRoot);

	auto left = dockRoot->addItem<PanelView>("LeftPanel", 0);
	left->setColor(RP_COLOR_BACKGROUND);
	left->setSizingPolicy(SizingPolicy::FitToParent);

	auto tabArea = dockRoot->addItem<DockTabView>("Tab Area", 1);
	tabArea->setSizingPolicy(SizingPolicy::FitToParent);

	_propGrid = dockRoot->addItem<ObjectInspectorView>("RightPanel", 2);
	_propGrid->setSizingPolicy(SizingPolicy::FitToParent);

	rootPanel->setLayoutDirty();

	auto tab1 = tabArea->addChild<SceneEditorView>("Scene Editor");
	tab1->setSizingPolicy(SizingPolicy::FitToParent);

	auto tab2 = tabArea->addChild<PanelView>("Green");
	tab2->setColor(Color4(0, 255, 0, 255));
	tab2->setSizingPolicy(SizingPolicy::FitToParent);

	/*_waveView = tabArea->addChild<WaveView>("Wave");
	_waveView->setSizingPolicy(SizingPolicy::FitToParent);
	generateWaveform();*/

	auto whitney = tabArea->addChild<Whitney>("Whitney");
	whitney->setSizingPolicy(SizingPolicy::FitToParent);

	/*auto dockRoot = rootPanel->addChild<DockSplitter>("Vertical Split");
	dockRoot->setSplitDirection(SplitDirection::Vertical);
	dockRoot->setSizingPolicy(SizingPolicy::FitToParent);
	//dockRoot->setArea({ 100, 100, 400, 200 });

	auto target2 = std::make_shared<DockSplitter>();
	//target2->setColor(nvgRGBA(0, 255, 0, 255));
	target2->setSplitDirection(SplitDirection::Horizontal);
	target2->setSizingPolicy(SizingPolicy::FitToParent);
	dockRoot->addItem(target2, 0);

	auto target3 = std::make_shared<PanelView>();
	target3->setColor(nvgRGBA(255, 0, 0, 255));
	target3->setSizingPolicy(SizingPolicy::FitToParent);
	dockRoot->addItem(target3, 0);

	auto target4 = std::make_shared<PanelView>();
	target4->setColor(nvgRGBA(0, 0, 255, 255));
	target4->setSizingPolicy(SizingPolicy::FitToParent);
	dockRoot->addItem(target4, 0);

	auto target5 = std::make_shared<PanelView>();
	target5->setColor(nvgRGBA(255, 0, 0, 255));
	target5->setSizingPolicy(SizingPolicy::FitToParent);
	target2->addItem(target5, 0);

	auto target6 = std::make_shared<PanelView>();
	target6->setColor(nvgRGBA(0, 255, 0, 255));
	target6->setSizingPolicy(SizingPolicy::FitToParent);
	target2->addItem(target6, 0);

	auto target7 = std::make_shared<PanelView>();
	target7->setColor(nvgRGBA(0, 0, 255, 255));
	target7->setSizingPolicy(SizingPolicy::FitToParent);
	target2->addItem(target7, 0);*/

	//generateWaveform();

	/*_audioProcessor = _audioGraph.createProcessor();
	_outputNode = _audioGraph.addNode<OutputNode>();
	_sineNode = _audioGraph.addNode<SineNode>();
	_audioGraph.connectNodes(_sineNode, 0, _outputNode, 0);*/

	/*_audioManager.setCallback([&](f32* output, const f32* input, uint32 frameCount) {
		_audioProcessor->process(output, input, frameCount);

		auto outputNode = std::static_pointer_cast<OutputProcessor>(_audioProcessor->getNodes()[0]);
		AudioBuffer buffer = outputNode->getAudioInput(0);
		assert(buffer.size() == frameCount);

		memset(output, 0, frameCount * sizeof(f32) * 2);

		for (size_t i = 0; i < frameCount; ++i) {
			output[i * 2] = buffer.getBuffer().get(i) * 0.1f;
			output[i * 2 + 1] = output[i];
		}
	});

	_audioManager.start();*/
}

void UiDocking::onResize(Dimension dimensions) {
	//bgfx::reset((uint32_t)w, (uint32_t)h, BGFX_RESET_VSYNC);
	//bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
}

/*void UiDocking::onDrop(int count, const char** paths) {

}*/

bool UiDocking::onKey(VirtualKey::Enum key, bool down) {
	if (key == VirtualKey::Space && down) {
		generateWaveform();
		return true;
	}

	return false;
}

void UiDocking::generateWaveform() {
	Float32Buffer samples;
	AudioLoaderUtil::load("c:\\temp\\telewizor.wav", samples);
	size_t sampleCount = samples.size();

	//_waveView->setAudioData(std::move(samples));

	size_t offset = 0;
	size_t markerStep = 44100;
	while (offset < sampleCount) {
		//_waveView->addMarker((f32)offset);
		offset += markerStep;
	}
}
