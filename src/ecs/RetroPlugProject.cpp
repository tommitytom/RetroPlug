#include "RetroPlugProject.h"

#include "foundation/Replicator.h"
#include "SineGenerator.h"

namespace rp {
	RetroPlugProject::RetroPlugProject(fw::EventNode&& eventNode, fw::EventNode::NodeId targetNodeId) : _eventNode(std::move(eventNode)) {
		fw::Replicator::subscribe(_registry, _eventNode, targetNodeId, true);
		fw::Replicator::replicate<SineGenerator::ComponentType>(_registry);
	}

	RetroPlugProject::~RetroPlugProject() {
		fw::Replicator::shutdown(_registry);
	}

	void RetroPlugProject::onUpdate(f32 deltaTime) {
		// NOTE: Delta time is currently always 0
		fw::Replicator::beginUpdate(_registry);
		_eventNode.update();
		fw::Replicator::endUpdate(_registry);
	}
}
