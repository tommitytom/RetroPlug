#pragma once

#include "ui/View.h"
#include "core/Constants.h"

#include "RetroPlugProject.h"

namespace rp {
	class RetroPlugEcsView final : public fw::View {
		FwRegisterObject()

	private:
		RetroPlugProject& _project;
		uint32 _version = 0;
		entt::entity _selectedSystem = entt::null;

	public:
		RetroPlugEcsView(RetroPlugProject& project);
		~RetroPlugEcsView() = default;

		void onInitialize() override;

		bool onDrop(const std::vector<std::string>& paths) override;

		void onUpdate(f32 deltaTime) override;

		void onRender(fw::Canvas& canvas) override;

		bool onKey(const fw::KeyEvent& event) override;

	private:
		void rebuildUi();

		entt::registry& getRegistry() {
			return _project.getRegistry();
		}
	};
}
