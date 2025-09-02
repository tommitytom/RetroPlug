#pragma once

#include "core/CoreComponents.h"
#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"
#include "ui/View.h"
#include "ecs/RetroPlugComponents.h"

namespace rp {
	class EcsSystemOverlay : public fw::View {
		FwRegisterObject()
	public:
		EcsSystemOverlay() {
			//fitToParent();
			getLayout().setDimensions(100_pc);
			setFocusPolicy(fw::FocusPolicy::Click);
		}
	};

	class EcsLsdjOverlay : public EcsSystemOverlay {
		FwRegisterObject()
	private:
		LsdjComponent& _lsdj;

	public:
		EcsLsdjOverlay(LsdjComponent& lsdj) : _lsdj(lsdj) {}
		~EcsLsdjOverlay() = default;

		void onRender(fw::Canvas& canvas) override {
			canvas.fillRect(getDimensionsF(), fw::Color4F(0, 0, 0, 0.5f * getAlpha()));
		}
	};

	class LsdjHooks final : public SystemHook<SameBoyComponent> {
	public:
		void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const override;

		fw::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const override;
	};
}
