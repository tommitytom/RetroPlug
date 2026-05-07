#pragma once

#include "ui/SystemOverlay.h"
#include "lsdj/LsdjController.h"
#include "ui/SliderView.h"
#include "ui/WaveView.h"
#include "lsdj/SampleUtil.h"

namespace rp {
	class LsdjSystemOverlay : public SystemOverlay {
		FwRegisterObject()
	private:
		entt::entity _entity = entt::null;
		LsdjController _lsdj;
		orb::WaveViewPtr _waveView;
		KitIndex _currentKit = INVALID_KIT_INDEX;

	public:
		LsdjSystemOverlay(entt::entity e, LsdjController lsdj) : _entity(e), _lsdj(lsdj) {}
		~LsdjSystemOverlay() = default;

		void onInitialize() override {
#ifdef FW_PLATFORM_WEB
			return;
#endif
			//_currentKit = 0;
			return;

			LsdjEditableKit kit{};

			
			kit.name = "KIT";

			std::vector<std::string> paths = { "C:\\retro\\samples\\toolong.wav" };

			kit.effects = {
				GainEffect{
					.gain = 0.5f
				}
			};

			int32 i = 0;
			for (const std::string& path : paths) {
				if (path.ends_with(".wav")) {
					std::string name = std::filesystem::path(path).filename().string().substr(0, 3);
					std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });

					LsdjSampleComponent sampleComp{
						.name = name,
						.path = path
					};

					kit.samples.push_back(std::move(sampleComp));
				}
			}

			_currentKit = _lsdj.addKitComponent(_entity, { .id = 0, .kit = kit });
			/*
			_lsdj.setKitComponent(_entity, 0, LsdjKitComponent{ .path = "C:\\retro\\kits\\23. AMEN.kit" });
			_currentKit = 0;

			_waveView = addChild<orb::WaveView>("Waveform");
			_waveView->getLayout().setDimensions(orb::Dimension{ 256, 64 });
			_waveView->setScale(0.3333f);
			auto slider = addChild<orb::SliderView>("Slider");
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
			*/
		}

		bool onKey(const orb::KeyEvent& event) override {
			if (event.down && event.key == orb::VirtualKey::H) {
				
			}

			return false;
		}

		void onUpdate(f32 delta) override {
			if (_currentKit == INVALID_KIT_INDEX) return;

			lsdj::Rom rom = _lsdj.getLsdjRom(_entity);
			orb::Uint8Buffer kitData = rom.getKitSampleData(_currentKit, 0);

			std::vector<LsdjKitComponent> kits;
			_lsdj.getKits(_entity, kits);

			rom.eachKit([&](lsdj::Kit kit) {
				const uint32 kitIndex = (uint32)kit.getIndex();
				const std::string name = std::string(kit.getName());

				return;
			});

			if (_waveView) {
				orb::Float32Buffer target;
				lsdj::SampleUtil::convertNibblesToF32(kitData, target);
				_waveView->setAudioData(std::move(target), 1);
			}
			
		}

		bool onDrop(const std::vector<std::string>& paths) override {
			/*LsdjKitComponent comp;
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

					//orb::FsUtil::readFile(paths[0], sampleComp.data());

					samples.push_back(std::move(sampleComp));
				}
			}

			_lsdj.addKitComponent(_entity, std::move(comp));

			auto waveView = addChild<orb::WaveView>("Waveform");
			auto slider = addChild<orb::SliderView>("Slider");

			return true;
			*/

			return false;
		}

		void onRender(orb::Canvas& canvas) override {
			//canvas.fillRect(getDimensionsF(), orb::Color4F(0, 0, 0, 0.5f * getAlpha()));
		}
	};
}
