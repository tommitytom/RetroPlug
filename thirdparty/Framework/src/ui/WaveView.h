#pragma once

#include "ui/Overlay.h"
#include "ui/View.h"
#include "WaveformUtil.h"

namespace fw {
	const f32 WAVEFORM_SCALE = 1.0f;

	struct ZoomChangedEvent {
		f32 zoom = 1.0f;
	};

	class WaveView final : public View {
	public:
		struct Theme {
			Color4 foreground = Color4(255, 255, 255, 255);
			Color4 background = Color4(0, 0, 0, 255);
			Color4 selection = Color4(255, 0, 0, 255);
			Color4 waveform = Color4(75, 243, 167, 255);
			Color4 waveformSelected = Color4(19, 60, 41, 255);
		};

	private:
		Float32Buffer _audioData;
		uint32 _channelCount = 0;
		MultiChannelWaveform _waveform;
		Theme _theme;

		f32 _startOffset = 0.0f;
		f32 _zoom = 1.0f;

		uint64 _slicePosition = 0;
		uint64 _sliceSize = 0;
		
		uint64 _selectionOffset = 0;
		uint64 _selectionSize = 0;
		bool _selecting = false;
		Point _clickPos;

		PointF _wheelPosition;

	public:
		WaveView() { 
			setType<WaveView>(); 
			setFocusPolicy(FocusPolicy::Click);
		}
		~WaveView() {}

		bool hasSelection() const {
			return _selectionSize > 0;
		}

		bool sampleToPixel(uint64 sample, f32& pixel);

		size_t pixelToSample(f32 pixel);

		void onRender(Canvas& canvas) override;

		void onResize(const ResizeEvent& ev) override {
			updateSlice(ev.size);
		}

		bool onMouseScroll(PointF delta, Point position) override;

		void onScaleChanged(f32 scale) override {
			updateSlice(getDimensions());
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			_clickPos = position;
			_selecting = down;

			if (down) {
				_selectionOffset = pixelToSample((f32)position.x);
				_selectionSize = 0;
			}

			return true;
		}

		bool onMouseMove(Point pos) override {
			if (_selecting) {
				_selectionSize = pixelToSample((f32)pos.x) - _selectionOffset;
			}

			return true;
		}

		void setTheme(const WaveView::Theme& theme) {
			_theme = theme;
		}

		void setAudioData(Float32Buffer&& audioData, uint32 channelCount) {
			assert(channelCount > 0);
			assert(((uint32)audioData.size() % channelCount) == 0);

			_audioData = std::move(audioData);
			_slicePosition = 0;
			_sliceSize = _audioData.size() / channelCount;
			_channelCount = channelCount;
			updateSlice();
		}

		void clearWaveform() {
			_audioData = Float32Buffer();
			_waveform.clear();
		}

		void updateSlice() { updateSlice(getDimensions()); }

		void updateSlice(Dimension dimensions);

		uint32 getExpectedSampleCount() const {
			return (uint32)(getDimensions().w * WAVEFORM_SCALE);
		}

		uint32 getViewableSampleCount();

		uint64 getSlicePosition() const {
			return _slicePosition;
		}

		uint64 getSliceSize() const {
			return _sliceSize;
		}

		f32 getZoom() const {
			return _zoom;
		}
	};

	using WaveViewPtr = std::shared_ptr<WaveView>;

	struct MarkerAddedEvent {
		size_t idx;
		uint64 marker;
	};

	struct MarkerChangedEvent {
		size_t idx;
		uint64 marker;
	};

	struct MarkerRemovedEvent {
		size_t idx;
	};

	class WaveMarkerOverlay final : public Overlay<WaveView> {
	private:
		std::vector<uint64> _markers;
		bool _editing = false;
		int32 _dragging = -1;
		int32 _mouseOver = -1;

	public:
		WaveMarkerOverlay() {
			setType<WaveMarkerOverlay>();
			setFocusPolicy(FocusPolicy::Click);
		}
		~WaveMarkerOverlay() {}

		void setEditMode(bool editMode) {
			_editing = editMode;
			_dragging = -1;
		}

		void updateMouseOver(Point pos) {
			_mouseOver = markerAtPoint(pos);

			if (_mouseOver != -1) {
				setCursor(CursorType::ResizeEW);
			} else {
				setCursor(CursorType::Arrow);
			}
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point pos) override {
			if (!_editing) {
				return false;
			}

			WaveViewPtr parent = getSuper();

			updateMouseOver(pos);

			if (button == MouseButton::Left) {
				if (down) {
					if (_mouseOver != -1) {
						_dragging = _mouseOver;
					} else {
						uint64 sample = (uint64)parent->pixelToSample((f32)pos.x);
						addMarker(sample, true);
					}
				} else {
					_dragging = -1;
				}
			} else if (button == MouseButton::Right && down) {
				if (_mouseOver != -1) {
					_markers.erase(_markers.begin() + _mouseOver);
					emit(MarkerRemovedEvent{ (size_t)_mouseOver });
				}
			}

			updateMouseOver(pos);

			return true;
		}

		void onMouseLeave() override {
			_mouseOver = -1;
			setCursor(CursorType::Arrow);
		}

		bool onMouseMove(Point pos) override {
			if (!_editing) {
				return false;
			}

			if (_dragging != -1) {
				WaveViewPtr parent = getSuper();
				uint64 sample = (uint64)parent->pixelToSample((f32)pos.x);

				uint64 min = 1;
				uint64 max = parent->getSliceSize() - 1;
				
				if (_dragging > 0) {
					min = _markers[_dragging - 1] + 1;
				}

				if (_dragging < (int32)_markers.size() - 1) {
					max = _markers[_dragging + 1] - 1;
				}

				sample = MathUtil::clamp(sample, min, max);
				_markers[_dragging] = sample;

				emit(MarkerChangedEvent{ (size_t)_dragging, sample });
			} else {
				updateMouseOver(pos);
			}

			return true;
		}

		int32 markerAtPoint(Point pos) {
			bool found = false;

			const f32 MARKER_HANDLE_SIZE = 3;
			PointF posF = (PointF)pos;

			for (size_t i = 0; i < _markers.size(); ++i) {
				f32 pixel;
				if (getSuper()->sampleToPixel(_markers[i], pixel)) {
					if (posF.x > pixel - MARKER_HANDLE_SIZE && posF.x < pixel + MARKER_HANDLE_SIZE) {
						return (int32)i;
					}
				}
			}

			return -1;
		}

		bool getEditMode() const {
			return _editing;
		}

		void onRender(Canvas& canvas) override {
			WaveViewPtr parent = getSuper();

			// Draw outline
			if (_editing) {
				canvas.strokeRect((DimensionF)getDimensions(), Color4F::red);
			}

			if (_markers.size()) {
				for (size_t i = 0; i < _markers.size(); ++i) {
					f32 pixel;
					if (parent->sampleToPixel(_markers[i], pixel)) {
						Color4F color = _mouseOver == (int32)i || _dragging == (int32)i ? Color4F::white : Color4F::red;
						canvas.line(PointF(pixel, 0), PointF(pixel, getDimensionsF().h), color);
					}
				}
			}
		}

		void addMarker(uint64 sample, bool fireEvent) {
			bool found = false;

			if (_markers.size() > 0) {
				uint64 last = 0;

				for (size_t i = 0; i < _markers.size(); ++i) {
					if (sample > last && sample < _markers[i]) {
						_markers.insert(_markers.begin() + i, sample);
						if (fireEvent) { emit(MarkerAddedEvent{ i, sample }); }

						found = true;
						break;
					}

					last = _markers[i];
				}
			}

			if (!found) {
				_markers.push_back(sample);
				if (fireEvent) { emit(MarkerAddedEvent{ _markers.size() - 1, sample }); }
			}
		}

		void setMarkers(std::vector<uint64>&& markers) {
			_markers = std::move(markers);
		}
	};

	using WaveMarkerOverlayPtr = std::shared_ptr<WaveMarkerOverlay>;
}
