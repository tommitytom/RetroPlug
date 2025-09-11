#pragma once

#include "core/CoreComponents.h"
#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"
#include "ui/View.h"
#include "ecs/RetroPlugComponents.h"
#include "ecs/LsdjController.h"

#include <ctype.h>

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
		entt::entity _entity = entt::null;
		LsdjController _lsdj;

	public:
		EcsLsdjOverlay(entt::entity e, LsdjController lsdj) : _entity(e), _lsdj(lsdj) {}
		~EcsLsdjOverlay() = default;

		bool onDrop(const std::vector<std::string>& paths) override {
			LsdjKitComponent comp;
			comp.name = "KIT";

			int32 i = 0;
			for (const std::string& path: paths) {
				auto& samples = comp.samples.emplace();
				if (path.ends_with(".wav")) {
					std::string name = std::filesystem::path(path).filename().string().substr(0, 3);
					std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });

					LsdjSampleComponent sampleComp{
						.name = name,
						.path = path
					};

					fw::FsUtil::readFile(paths[0], sampleComp.data());

					samples.push_back(std::move(sampleComp));
				}
			}

			_lsdj.addKitComponent(_entity, std::move(comp));

			return true;
		}

		void onRender(fw::Canvas& canvas) override {
			//canvas.fillRect(getDimensionsF(), fw::Color4F(0, 0, 0, 0.5f * getAlpha()));
		}
	};

	class LsdjHooks final : public SystemHook<SameBoyComponent> {
	public:
		void onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const override;

		void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const override;

		fw::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const override;

		void onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const override;

		void onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const override;
	};
}
