#pragma once

#include "ui/View.h"
#include "WaveformUtil.h"

namespace rp {
	const f32 WAVEFORM_SCALE = 1.0f;

	class WaveView final : public View {
	public:
		struct Theme {
			Color4 foreground = Color4(255, 255, 255, 255);
			Color4 background = Color4(0, 0, 0, 255);
			Color4 selection = Color4(255, 0, 0, 255);
		};

	private:
		Float32Buffer _audioData;
		Waveform _waveform;
		Theme _theme;

		f32 _startOffset = 0.0f;
		f32 _zoom = 1.0f;
		
		size_t _slicePosition = 0;
		size_t _sliceSize = 0;

		std::vector<f32> _markers;

	public:
		WaveView() { setType<WaveView>(); }
		~WaveView() {}

		void addMarker(f32 sampleOffset) {
			_markers.push_back(sampleOffset);
		}

		void setMarkers(std::vector<f32>&& markers) {
			_markers = std::move(markers);
		}

		void onRender(Canvas& canvas) override;

		void onResize(uint32 w, uint32 h) override {
			updateSlice();
		}

		bool onMouseScroll(PointT<f32> delta, Point position) override;

		void onScaleChanged(f32 scale) override {
			updateSlice();
		}

		void setTheme(const WaveView::Theme& theme) {
			_theme = theme;
		}

		void setAudioData(Float32Buffer&& audioData) {
			_audioData = std::move(audioData);
			_slicePosition = 0;
			_sliceSize = _audioData.size();
			_markers.clear();
			updateSlice();
		}

		void clearWaveform() {
			_audioData = Float32Buffer();
			_markers.clear();
			_waveform.linePoints.clear();
		}

		uint32 getExpectedSampleCount() const {
			return (uint32)(getDimensions().w * WAVEFORM_SCALE);
		}

		void updateSlice();
	};

	using WaveViewPtr = std::shared_ptr<WaveView>;
}
