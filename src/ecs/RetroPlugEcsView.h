#pragma once

#include "ui/View.h"
#include "core/Constants.h"

#include "RetroPlugProject.h"

namespace rp {
	class RetroPlugEcsView final : public fw::View {
		FwRegisterObject()

	private:
		RetroPlugProjectPtr _project;

	public:
		RetroPlugEcsView(const RetroPlugProjectPtr& project);
		~RetroPlugEcsView() = default;

		void onInitialize() override;

		void onUpdate(f32 deltaTime) override;

		void onRender(fw::Canvas& canvas) override;

		bool onKey(const fw::KeyEvent& event) override;

	private:
		void rebuildUi();

		entt::registry& getRegistry() {
			return _project->getRegistry();
		}
	};
}
