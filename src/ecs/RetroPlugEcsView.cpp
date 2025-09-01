#include "RetroPlugEcsView.h"

#include "foundation/Replicator.h"
#include "ui/SliderView.h"
#include "Components.h"
#include "SineGenerator.h"

namespace rp {
	RetroPlugEcsView::RetroPlugEcsView(const RetroPlugProjectPtr& project) : View({ 480, 432 }), _project(project) {
		setName(fmt::format("RetroPlug v{}", RP_VERSION));
		setFocusPolicy(fw::FocusPolicy::Click);
	}

	void RetroPlugEcsView::onInitialize() {
		
	}

	void RetroPlugEcsView::onUpdate(f32 deltaTime) {
		_project->onUpdate(deltaTime);
	}

	void RetroPlugEcsView::onRender(fw::Canvas& canvas) {
		canvas.fillRect(getDimensions(), fw::Color4F::red);
	}

	bool RetroPlugEcsView::onKey(const fw::KeyEvent& event) {
		if (event.down && event.key == fw::VirtualKey::F5) {
			entt::registry& registry = _project->getRegistry();

			entt::entity e = SineGenerator::emplace(registry, fw::Replicator::spawn(registry));

			auto slider4 = addChild<fw::SliderView>("Frequency Slider");
			slider4->setArea({ 10, 150, 300, 30 });
			slider4->setRange(20.0f, 5000.0f);
			slider4->ValueChangeEvent = [&registry, e](f32 value) {
				fw::Replicator::patchField<&SineComponent::frequency>(registry, e, value);
			};

			return true;
		}

		return false;
	}
}
