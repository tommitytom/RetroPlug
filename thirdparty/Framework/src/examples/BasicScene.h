#pragma once

#include "foundation/Math.h"
#include "engine/EngineSystems.h"
#include "engine/SceneView.h"
#include "ui/View.h"
#include "ui/SliderView.h"
#include "foundation/Event.h"
#include "audio/AudioManager.h"

#include <cmath>

namespace fw {
	struct FreqChangeEvent {
		f32 freq = 200.0f;
	};

	struct AmpChangeEvent {
		f32 amp = 0.2f;
	};

	class SineOsc {
	private:
		f32 _frequency = 200.0f;
		f32 _phase = 0.0f;
		f32 _step = 0.0f;
		f32 _sampleRate = 48000;

	public:
		SineOsc(f32 freq, f32 sampleRate) : _frequency(freq), _sampleRate(sampleRate) { updateStep(); }
		~SineOsc() = default;

		void setFreq(f32 freq) {
			_frequency = freq;
			updateStep();
		}

		void setSampleRate(f32 sr) {
			_sampleRate = sr;
			updateStep();
		}

		f32 next() {
			f32 val = sin(_phase);
			_phase = fmod(_phase + _step, PI2);
			return val;
		}

	private:
		void updateStep() {
			_step = _frequency / _sampleRate;
		}
	};

	class SineBankOsc {
	private:
		std::vector<std::pair<SineOsc, f32>> _oscilators;

	public:
		SineBankOsc(size_t count, f32 sampleRate) { 
			_oscilators.reserve(count); 

			for (size_t i = 0; i < count; ++i) {
				_oscilators.push_back({ SineOsc(0.0f, sampleRate), 0.0f });
			}
		}

		void setOscSettings(size_t idx, f32 freq, f32 amp) {
			_oscilators[idx].first.setFreq(freq);
			_oscilators[idx].second = amp;
		}

		void setSampleRate(f32 sampleRate) {
			for (size_t i = 0; i < _oscilators.size(); ++i) {
				_oscilators[i].first.setSampleRate(sampleRate);
			}
		}

		f32 next() {
			f32 val = 0.0f;
			for (std::pair<SineOsc, f32>& osc : _oscilators) { val += osc.first.next() * osc.second; }
			return val;
		}

		size_t getOscCount() const {
			return _oscilators.size();
		}
	};

	class MyAudioProceessor : public AudioProcessor {
	private:
		EventNode _eventNode;

		f32 _amp = 0.1f;
		SineOsc _osc;
		SineBankOsc _bank;

	public:
		MyAudioProceessor(EventNode&& eventNode): _eventNode(std::move(eventNode)), _osc(200.0f, 48000.0f), _bank(32, 48000.0f) {
			_eventNode.subscribe<FreqChangeEvent>([&](const FreqChangeEvent& ev) {
				f32 fs = _bank.getOscCount();

				for (size_t i = 0; i < _bank.getOscCount(); ++i) {
					f32 fi = (f32)i;
					f32 ratio = fi / (fs - 1.0f);

					//f32 dist = spacing * (fi + 1.0f);

					_bank.setOscSettings(i, ev.freq + (i * ev.freq), 1.0f - ratio);
				}
			});

			_eventNode.subscribe<AmpChangeEvent>([&](const AmpChangeEvent& ev) {
				_amp = ev.amp;
			});
		}

		~MyAudioProceessor() = default;

		f32 getSampleRate() const {
			return 48000.0f;
		}		

		void onRender(f32* output, const f32* input, uint32 frameCount) override {
			_eventNode.update();

			for (uint32 i = 0; i < frameCount; ++i) {
				output[i * 2] = _bank.next() * _amp;
				output[i * 2 + 1] = output[i * 2];
			}
		}
	};

	class BasicScene : public SceneView {
	private:
		entt::entity _camera = entt::null;
		entt::entity _ball = entt::null;
		entt::entity _grid = entt::null;
		
		f32 _cameraMoveSpeed = 50.0f;
		PointF _velocity;

	public:
		BasicScene() : SceneView({ 1024, 768 }) { setType<BasicScene>(); }
		~BasicScene() = default;

		void onInitialize() override {
			SceneView::onInitialize();

			entt::registry& reg = getRegistry();

			_camera = EngineUtil::createOrthographicCamera(reg);
			_ball = EngineUtil::createSprite(reg, "textures/circle-512.png");

			_grid = WorldUtil::createEntity(reg);
			reg.emplace<GridComponent>(_grid);

			EventNode* eventNode = createState<EventNode>({ "Main" });

			auto audioManager = getState<std::shared_ptr<audio::AudioManager>>();
			audioManager->get()->setProcessor(std::make_shared<MyAudioProceessor>(eventNode->spawn("Audio")));

			auto slider = addChild<SliderView>("Freq slider");
			slider->setArea({ 10, 10, 500, 40 });
			slider->setRange(50.0f, 10000.0f);
			slider->ValueChangeEvent = [eventNode](f32 value) { eventNode->broadcast(FreqChangeEvent{ value }); };

			auto slider2 = addChild<SliderView>("Amp slider");
			slider2->setArea({ 10, 60, 500, 40 });
			slider2->setRange(0.0f, 0.3f);
			slider2->ValueChangeEvent = [eventNode](f32 value) { eventNode->broadcast(AmpChangeEvent{ value }); };
		}

		void onUpdate(f32 delta) override {
			entt::registry& reg = getRegistry();

			getState<EventNode>()->update();

			TransformComponent& cameraTrans = reg.get<TransformComponent>(_camera);
			cameraTrans.position += _velocity * delta;

			SceneView::onUpdate(delta);
		}

		void onRender(Canvas& canvas) override {
			entt::registry& reg = getRegistry();

			beginSceneRender(reg, canvas);
			GridRenderSystem::update(reg, canvas);
			SpriteRenderSystem::update(reg, canvas);
			endSceneRender(canvas);
		}

		bool onMouseScroll(PointF delta, Point position) override { 
			getRegistry().get<OrthographicCameraComponent>(_camera).zoom += delta.y * 0.01f;
			return true; 
		}

		bool onKey(VirtualKey::Enum key, bool down) override {
			_cameraMoveSpeed = 300;
			switch (key) {
			case VirtualKey::LeftArrow: _velocity.x = down ? -_cameraMoveSpeed : 0.0f; break;
			case VirtualKey::RightArrow: _velocity.x = down ? _cameraMoveSpeed : 0.0f; break;
			case VirtualKey::UpArrow: _velocity.y = down ? -_cameraMoveSpeed : 0.0f; break;
			case VirtualKey::DownArrow: _velocity.y = down ? _cameraMoveSpeed : 0.0f; break;
			}

			return true;
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			return true;
		}
	};
}
