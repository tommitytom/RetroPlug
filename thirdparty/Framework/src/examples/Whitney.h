#pragma once

#include <stb/stb_image.h>

#include "foundation/Math.h"
#include "foundation/PropertyModulator.h"

#include "ui/DropDownMenuView.h"
#include "ui/ObjectInspectorView.h"
#include "ui/PropertyEditorView.h"
#include "ui/SliderView.h"
#include "ui/View.h"

#include "textures/circle-512.h"

#include "application/Application.h"

#include "ui/ObjectInspectorUtil.h"

namespace fw {
	struct WhitneySettings {
		size_t dotCount = 1250;
		f32 duration = 280;
		f32 minSize = 0.5f;
		f32 maxSize = 50.0f;
		f32 dotAlpha = 0.9f;
		f32 lineAlpha = 0.9f;
		f32 hueOffset = 0.0f;
		bool drawDots = true;
		bool drawLines = true;
		bool dotsOverLines = true;
	};
}

REFL_AUTO(
	type(fw::WhitneySettings),
	field(dotCount, fw::RangeAttribute(2, 20000)),
	field(duration, fw::RangeAttribute(10.0f, 20000)/*, fw::CurveAttribute(Curves::pow2)*/),
	field(minSize, fw::RangeAttribute(0.01f, 50.0f)),
	field(maxSize, fw::RangeAttribute(0.01f, 50.0f)),
	field(dotAlpha),
	field(lineAlpha),
	field(hueOffset),
	field(drawDots),
	field(drawLines),
	field(dotsOverLines)
)

REFL_AUTO(
	type(fw::PropertyModulator),
	field(type),
	field(mode),
	field(timing),
	field(frequency),
	field(multiplier),
	field(range),
	field(position),
	field(cycleLength)
)

namespace fw {
	struct Dot {
		Point pos;
		RectF area;
		Color4F dotColor;
		Color4F lineColor;
	};

	class Whitney : public View {
	private:
		WhitneySettings _baseSettings;
		WhitneySettings _settings;

		std::vector<PropertyModulator::Target> _modTargets;
		std::vector<std::string> _modTargetNames;
		std::vector<std::pair<PropertyModulator, PropertyEditorBasePtr>> _modulators;

		bool _playing = true;
		f32 _position = 0.0f;
		f32 _lastDuration = 0.0f;
		bool _dirty = true;
		TextureHandle _circle;

		ObjectInspectorViewPtr _objectInspector;
		SliderViewPtr _positionSlider;

		std::vector<Dot> _dots;
		
	public:
		Whitney() : View({ 1024, 768 }) {
			setType<Whitney>();
			setFocusPolicy(FocusPolicy::Click);
		}

		~Whitney() = default;

		void onInitialize() override {
			getLayout().setDimensions(100_pc);
			//_dotCountSlider->setCurve(SliderScaler::pow2);
			//_durationSlider->setCurve(SliderScaler::pow2);

			Dimension dim;
			int comp;
			stbi_uc* imageData = stbi_load_from_memory((const stbi_uc*)circle_512, (int)circle_512_len, &dim.w, &dim.h, &comp, 0);

			std::vector<uint8> circleImage;
			circleImage.resize(dim.w * dim.h * comp);
			memcpy(circleImage.data(), imageData, circleImage.size());

			_modulators.reserve(8);
			_circle = getResourceManager().create<Texture>(TextureDesc {
				.dimensions = { 512, 512 },
				.depth = 4,
				.data = circleImage
			});

			//_circle = getResourceManager().load<Texture>("C:\\code\\RetroPlugNext\\thirdparty\\Framework\\resources\\textures\\circle-512.png");

			for (size_t i = 0; i < _baseSettings.dotCount; ++i) {
				_dots.push_back(Dot());
			}

			for_each(refl::reflect<WhitneySettings>().members, [&](auto member) {
				using MemberType = std::decay_t<decltype(member(_baseSettings))>;

				if constexpr (is_writable(member)/* && refl::descriptor::has_attribute<serializable>(member)*/) {
					std::string fieldName = StringUtil::formatMemberName(get_display_name(member));
					
					_modTargetNames.push_back(fieldName);
					
					_modTargets.push_back(PropertyModulator::Target{
						.name = std::move(fieldName),
						.source = entt::any(member(_baseSettings)),
						.target = entt::any(member(_settings))
					});

					assert(_modTargets.back().source.owner());
					assert(_modTargets.back().target.owner());
				}
			});

			/*for (const fw::Field& field : _typeRegistry.getTypeInfo<Settings>().fields) {
				if (field.type != getTypeId<f32>()) {
					continue;
				}

				std::string fieldName = StringUtil::formatMemberName(field.name);
				_modTargetNames.push_back(fieldName);

				assert(!field.get(_baseSettings).owner());
				assert(!field.get(_settings).owner());

				_modTargets.push_back(PropertyModulator::Target{
					.name = std::move(fieldName),
					.field = &field,
					.source = field.get(_baseSettings).as_ref(),
					.target = field.get(_settings).as_ref()
				});

				assert(!_modTargets.back().source.owner());
				assert(!_modTargets.back().target.owner());
			}*/

			//addModulator();
			//addModulator();

			rebuildPropertyGrid();
		}

		void rebuildPropertyGrid() {
			if (_objectInspector) {
				_objectInspector->remove();
			}

			_objectInspector = addChildAt<ObjectInspectorView>("Property Editor", { 50, 50 });
			_objectInspector->getLayout().setFlexPositionType(FlexPositionType::Absolute);
			_objectInspector->getLayout().setDimensions(Dimension{ 300, 600 });

			_positionSlider = _objectInspector->addProperty<SliderView>("Position");
			_positionSlider->setRange(0, _settings.duration);
			_positionSlider->ValueChangeEvent = [&](f32 value) {
				_position = value;
				_dirty = true;
			};

			ObjectInspectorUtil::reflect(_objectInspector, _baseSettings);

			for (size_t i = 0; i < _modulators.size(); ++i) {
				_objectInspector->pushGroup(fmt::format("Modulator {}", i + 1));

				DropDownMenuViewPtr modTarget = _objectInspector->addProperty<DropDownMenuView>("Target");
				modTarget->setItems(_modTargetNames);

				/*modTarget->ValueChangeEvent = [i, this](int32 index) {
					if (index >= 0) {
						PropertyModulator::Target& sourceTarget = _modTargets[index];
						auto editor = _objectInspector->findEditor<SliderView>(*sourceTarget.field);

						if (_modulators[i].second) {
							SliderViewPtr slider = _modulators[i].second->asShared<SliderView>();
							slider->setHandleCount(1);
						}

						_modulators[i].first.targets.clear();
						_modulators[i].first.targets.push_back(PropertyModulator::Target{
							.name = sourceTarget.name,
							.field = sourceTarget.field,
							.source = std::move(sourceTarget.source.as_ref()),
							.target = std::move(sourceTarget.target.as_ref()),
						});

						editor->setHandleCount(2);
						_modulators[i].second = editor;
					}
				};*/

				//_objectInspector->addEnumProperty<PropertyModulator::Type>(_modulators[i].first);

				DropDownMenuViewPtr typeMenu = _objectInspector->addProperty<DropDownMenuView>("Type");
				typeMenu->setItems(magic_enum::enum_names<PropertyModulator::Type>());
				typeMenu->ValueChangeEvent = [i, this](int32 index) { _modulators[i].first.type = (PropertyModulator::Type)index; };

				DropDownMenuViewPtr modeMenu = _objectInspector->addProperty<DropDownMenuView>("Mode");
				modeMenu->setItems(magic_enum::enum_names<PropertyModulator::Mode>());
				modeMenu->ValueChangeEvent = [i, this](int32 index) { _modulators[i].first.mode = (PropertyModulator::Mode)index; };

				DropDownMenuViewPtr timingMenu = _objectInspector->addProperty<DropDownMenuView>("Timing");
				timingMenu->setItems(magic_enum::enum_names<PropertyModulator::Timing>());
				timingMenu->ValueChangeEvent = [i, this](int32 index) { _modulators[i].first.timing = (PropertyModulator::Timing)index; };

				if (_modulators[i].first.timing == PropertyModulator::Timing::Frequency) {
					SliderViewPtr freqSlider = _objectInspector->addProperty<SliderView>("Frequency");
					freqSlider->setRange(0.01f, 1.0f);
					freqSlider->ValueChangeEvent = [i, this](f32 val) { _modulators[i].first.frequency = val; };
				} else {
					SliderViewPtr freqSlider = _objectInspector->addProperty<SliderView>("Multiplier");
					freqSlider->setRange(0.01f, 1.0f);
					freqSlider->ValueChangeEvent = [i, this](f32 val) {
						_modulators[i].first.frequency = val / _baseSettings.duration;
					};
				}

				SliderViewPtr rangeSlider = _objectInspector->addProperty<SliderView>("Range");
				rangeSlider->setRange(0.0f, 1.0f);
				rangeSlider->ValueChangeEvent = [i, this](f32 val) { _modulators[i].first.range = val; };
			}
		}

		void addModulator() {
			_modulators.push_back(std::pair<PropertyModulator, PropertyEditorBasePtr>());
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			return true;
		}

		bool onKey(VirtualKey::Enum key, bool down) override {
			if (key == VirtualKey::Space && !down) {
				_playing = !_playing;
			}

			if (key == VirtualKey::Q && !down) {
				rebuildPropertyGrid();
			}

			return true;
		}

		void onUpdate(f32 delta) override {
			if (_playing) {
				_settings = _baseSettings;

				for (auto& mod : _modulators) {
					mod.first.update(delta);

					for (PropertyModulator::Target& target : mod.first.targets) {
						mod.second->asShared<SliderView>()->setValueAt(1, entt::any_cast<f32>(target.target));
					}
				}
			}

			if (_lastDuration != _settings.duration) {
				f32 scale = _settings.duration / _lastDuration;
				_position *= scale;

				_positionSlider->setRange(0, _settings.duration);
				_positionSlider->setValue(_position);

				_lastDuration = _settings.duration;
				_dirty = true;
			}

			if (_dots.size() != _settings.dotCount) {
				while (_settings.dotCount > _dots.size()) {
					_dots.push_back(Dot());
				}

				while (_settings.dotCount < _dots.size()) {
					_dots.pop_back();
				}

				_dirty = true;
			}

			if (_playing) {
				_position += delta;
				_position = fmod(_position, _settings.duration);
				_positionSlider->setValue(_position);
				_dirty = true;
			}

			if (_dirty) {
				f32 phase = (_position / _settings.duration) * PI2;

				PointF mid((f32)getDimensions().w / 2, (f32)getDimensions().h / 2);

				f32 spacing = mid.y / _dots.size();
				f32 sizeRange = _settings.maxSize - _settings.minSize;
				f32 fs = (f32)_dots.size();

				for (size_t i = 0; i < _dots.size(); ++i) {
					f32 fi = (f32)i;

					f32 ratio = fi / (fs - 1.0f);
					f32 dist = spacing * (fi + 1.0f);
					f32 dotPhase = phase * (fs - fi);
					f32 size = (ratio * sizeRange) + _settings.minSize;
					f32 halfSize = size * 0.5f;

					PointF pos(cos(dotPhase), sin(dotPhase));
					pos *= dist;
					pos += mid;
					pos -= halfSize;

					_dots[i].area = RectF(pos, { size, size });
					getColor(ratio, _settings.dotAlpha, _dots[i].dotColor);
					_dots[i].lineColor = _dots[i].dotColor;
					_dots[i].lineColor.a = _settings.lineAlpha;
				}
			}
		}

		void onRender(fw::Canvas& canvas) override {
			if (_settings.dotsOverLines) {
				if (_settings.drawLines) { renderLines(canvas); }
				if (_settings.drawDots) { renderDots(canvas); }
			} else {
				if (_settings.drawDots) { renderDots(canvas); }
				if (_settings.drawLines) { renderLines(canvas); }
			}
		}

	private:
		void renderLines(fw::Canvas& canvas) const {
			std::vector<PointF> points(_dots.size());

			for (size_t i = 0; i < _dots.size() - 1; ++i) {
				PointF d1 = _dots[i].area.getCenter();
				PointF d2 = _dots[i + 1].area.getCenter();
				canvas.line(d1, d2, _dots[i].lineColor);
			}
		}

		void renderDots(fw::Canvas& canvas) const {
			for (size_t i = 0; i < _dots.size(); ++i) {
				canvas.texture(_circle, _dots[i].area, _dots[i].dotColor);
			}
		}

		void getColor(f32 ratio, f32 alpha, Color4F& v) {
			f32 r = fmod(ratio + _settings.hueOffset, 1.0f) * PI2;
			v.r = (0.5f + cos(r) * 0.5f);
			v.g = (0.5f + cos(r + PI2 / 3.0f) * 0.5f);
			v.b = (0.5f + cos(r + PI2 * 2.0f / 3.0f) * 0.5f);
			v.a = alpha;
		}
	};

	using WhitneyApplication = fw::app::BasicApplication<Whitney, void>;
}
