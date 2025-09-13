#pragma once

#include "ecs/EcsSystemOverlay.h"
#include "ecs/LsdjController.h"
#include "ui/SliderView.h"
#include "ui/WaveView.h"
#include "lsdj/SampleUtil.h"

namespace rp {
	class EcsLsdjOverlay : public EcsSystemOverlay {
		FwRegisterObject()
	private:
		entt::entity _entity = entt::null;
		LsdjController _lsdj;
		fw::WaveViewPtr _waveView;
		KitIndex _currentKit = INVALID_KIT_INDEX;

	public:
		EcsLsdjOverlay(entt::entity e, LsdjController lsdj) : _entity(e), _lsdj(lsdj) {}
		~EcsLsdjOverlay() = default;

		void onInitialize() override {
#ifdef FW_PLATFORM_WEB
			return;
#endif
			return;
			LsdjKitComponent comp;
			comp.name = "KIT";

			std::vector<std::string> paths = { "C:\\retro\\samples\\mule\\kick.wav" };

			comp.effects = {
				GainEffect{
					.gain = 0.5f
				}
			};

			int32 i = 0;
			for (const std::string& path : paths) {
				auto& samples = comp.samples.emplace();
				if (path.ends_with(".wav")) {
					std::string name = std::filesystem::path(path).filename().string().substr(0, 3);
					std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });

					LsdjSampleComponent sampleComp{
						.name = name,
						.path = path
					};

					//fw::FsUtil::readFile(paths[0], sampleComp.data());

					samples.push_back(std::move(sampleComp));
				}
			}

			//_currentKit = _lsdj.addKitComponent(_entity, std::move(comp));

			_lsdj.setKitComponent(_entity, 0, LsdjKitComponent{ .path = "C:\\retro\\kits\\23. AMEN.kit" });
			_currentKit = 0;

			_waveView = addChild<fw::WaveView>("Waveform");
			_waveView->getLayout().setDimensions(fw::Dimension{ 256, 64 });
			_waveView->setScale(0.3333f);
			auto slider = addChild<fw::SliderView>("Slider");
			slider->setScale(0.3333f);

			slider->ValueChangeEvent = [this](f32 val) {
				if (_currentKit != INVALID_KIT_INDEX) {
					LsdjKitComponent* kit = _lsdj.getKitComponent(_entity, _currentKit);

					kit->effects.value()[0].visit([&](auto&& eff) {
						if constexpr (std::is_same_v<std::decay_t<decltype(eff)>, GainEffect>) {
							eff.gain = val;
						}
					});

					_lsdj.setKitDirty(_entity, _currentKit);
				}
			};
		}

		void onUpdate(f32 delta) override {
			if (!_waveView || _currentKit == INVALID_KIT_INDEX) return;

			lsdj::Rom rom = _lsdj.getLsdjRom(_entity);
			fw::Uint8Buffer kitData = rom.getKitSampleData(_currentKit, 0);
			
			fw::Float32Buffer target;
			lsdj::SampleUtil::convertNibblesToF32(kitData, target);
			_waveView->setAudioData(std::move(target), 1);
		}

		bool onDrop(const std::vector<std::string>& paths) override {
			LsdjKitComponent comp;
			comp.name = "KIT";

			int32 i = 0;
			for (const std::string& path : paths) {
				auto& samples = comp.samples.emplace();
				if (path.ends_with(".wav")) {
					std::string name = std::filesystem::path(path).filename().string().substr(0, 3);
					std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });

					LsdjSampleComponent sampleComp{
						.name = name,
						.path = path
					};

					//fw::FsUtil::readFile(paths[0], sampleComp.data());

					samples.push_back(std::move(sampleComp));
				}
			}

			_lsdj.addKitComponent(_entity, std::move(comp));

			auto waveView = addChild<fw::WaveView>("Waveform");
			auto slider = addChild<fw::SliderView>("Slider");

			return true;
		}

		void onRender(fw::Canvas& canvas) override {
			//canvas.fillRect(getDimensionsF(), fw::Color4F(0, 0, 0, 0.5f * getAlpha()));
		}
	};
}
