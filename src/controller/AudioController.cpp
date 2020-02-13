#include "AudioController.h"

void AudioController::setNode(Node* node) {
	_node = node;
	node->getAllocator()->reserveChunks(160 * 144 * 4, 16); // Video buffers

	_processingContext.setNode(node);

	node->on<calls::SwapInstance>([&](const InstanceSwapDesc& d, SameBoyPlugPtr& other) {
		_lua.addInstance(d.idx, d.instance);
		other = _processingContext.swapInstance(d.idx, d.instance);
	});

	node->on<calls::DuplicateInstance>([&](const InstanceDuplicateDesc& d, SameBoyPlugPtr& other) {
		_lua.addInstance(d.targetIdx, d.instance);
		other = _processingContext.duplicateInstance(d.sourceIdx, d.targetIdx, d.instance);
	});

	node->on<calls::TakeInstance>([&](const InstanceIndex& idx, SameBoyPlugPtr& other) {
		_lua.removeInstance(idx);
		other = _processingContext.removeInstance(idx);
	});

	node->on<calls::UpdateSettings>([&](const Project::Settings& settings) {
		_processingContext.setSettings(settings);
	});

	node->on<calls::FetchState>([&](const FetchStateRequest& req, FetchStateResponse& state) {
		_processingContext.fetchState(req, state);
	});

	node->on<calls::PressButtons>([&](const ButtonStream<32>& presses) {
		SameBoyPlugPtr& instance = _processingContext.getInstance(presses.idx);
		if (instance) {
			instance->pressButtons(presses.presses.data(), presses.pressCount);
		}
	});
}
