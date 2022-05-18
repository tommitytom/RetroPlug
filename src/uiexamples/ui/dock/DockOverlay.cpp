#include "DockOverlay.h"

using namespace rp;

void DockOverlay::onUpdate(f32 delta) {
	const DragContext& ctx = getDragContext();
	if (!ctx.isDragging) {
		return;
	}

	for (const ViewPtr target : ctx.targets) {
		if (target->isType<DockPanel>()) {
			const auto& dropTargets = target->asShared<DockPanel>()->getDropTargets();
		}
	}

	// Find all windows
	// Check if any of them are in drag/drop mode
}

void DockOverlay::onRender() {
	if (!_dragOver) {
		return;
	}

	for (size_t i = 0; i < _dropTargets.size(); ++i) {
		const Rect<uint32>& target = _dropTargets[i];

		if (target != Rect<uint32>()) {
			NVGcolor color = nvgRGBA(0, 255, 0, 255);

			if ((DropTargetType)i == _dragOverIdx) {
				color = nvgRGBA(0, 0, 255, 255);
			}

			drawRect(target, color);
		}
	}

	NVGcolor highlightColor = nvgRGBA(0, 50, 255, 120);
	uint32 highlightMargin = 5;

	if (_dragOverIdx != DropTargetType::None) {
		Rect<uint32> dim;

		switch (_dragOverIdx) {
		case DropTargetType::Center:
			dim = getDimensions();
			break;
		case DropTargetType::Top:
			dim = { 0, 0, getDimensions().w, getDimensions().h / 2 };
			break;
		case DropTargetType::Right:
			dim = { getDimensions().w / 2, 0, getDimensions().w / 2, getDimensions().h };
			break;
		case DropTargetType::Bottom:
			dim = { 0, getDimensions().h / 2, getDimensions().w, getDimensions().h / 2 };
			break;
		case DropTargetType::Left:
			dim = { 0, 0, getDimensions().w / 2, getDimensions().h };
			break;
		}

		drawRect(dim.shrink(highlightMargin), highlightColor);
	}
}
