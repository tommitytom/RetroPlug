#include "WaveView.h"

#include <spdlog/spdlog.h>

#include "WaveformUtil.h"
#include "foundation/Curves.h"

using namespace fw;

const f32 MIN_SAMPLES_VIEWABLE = 100.0f;
const f32 ZOOM_STEP_COUNT = 20.0f;

void WaveView::updateSlice(Dimension dimensions) {
	if (_audioData.size() == 0) {
		return;
	}

	_waveform.channels.resize(_channelCount);

	size_t sampleCount = (size_t)(dimensions.w * getWorldScale()) - 2;

	for (uint32 i = 0; i < _channelCount; ++i) {
		WaveformUtil::generate(_audioData.slice(_slicePosition * _channelCount, _sliceSize * _channelCount), _waveform.channels[i], sampleCount, i, _channelCount);
	}
}

uint32 WaveView::getViewableSampleCount() {
	return 0;
}

bool WaveView::onMouseScroll(PointF delta, Point position) {
	// Get the sample under the cursor

	f32 totalSize = (f32)(_audioData.size() / _channelCount);
	const f32 minSize = 50;

	f32 zoomPow = Curves::pow3(_zoom);

	f32 viewableSamples = totalSize * zoomPow;
	f32 frac = (f32)position.x / (f32)getDimensions().w;

	f32 stepSize = 1.0f / ZOOM_STEP_COUNT;
	_zoom = MathUtil::clamp(_zoom - delta.y * stepSize, 0.0f, 1.0f);
	zoomPow = Curves::pow3(_zoom);

	f32 newViewableSamples = MathUtil::clamp(totalSize * zoomPow, minSize, totalSize);
	f32 diff = viewableSamples - newViewableSamples;

	_startOffset += diff * frac;

	if (_startOffset + newViewableSamples > totalSize) {
		_startOffset = totalSize - newViewableSamples;
	}

	if (_startOffset < 0) {
		_startOffset = 0;
	}

	_slicePosition = (size_t)_startOffset;
	_sliceSize = (size_t)newViewableSamples;

	updateSlice();

	emit(ZoomChangedEvent{ _zoom });

	return true;
}

bool WaveView::sampleToPixel(uint64 sample, f32& pixel) {
	int64 ranged = (int64)sample - (int64)_slicePosition;
	f32 frac = (f32)ranged / (f32)_sliceSize;
	pixel = frac * getDimensionsF().w;
	return sample >= _slicePosition && sample < _slicePosition + _sliceSize;
}

size_t WaveView::pixelToSample(f32 pixel) {
	size_t targetSize = (size_t)(getDimensions().w * getWorldScale()) - 2;
	f32 chunkOffset = 0;

	if (_sliceSize > targetSize) {
		// TODO: Maybe get the sample in the middle of the chunk that makes up this pixel
		//chunkOffset = ((f32)_sliceSize / (f32)targetSize) * 0.5f;
	}

	f32 frac = pixel / getDimensionsF().w;
	return _slicePosition + (size_t)(_sliceSize * frac) + (size_t)chunkOffset;
}

struct WaveFormSection {
	f32 startPixel;
	f32 size;
	Color4F color;
};

void WaveView::onRender(fw::Canvas& canvas) {
	f32 scaleFactor = getWorldScale();
	RectF area = { 0, 0, (f32)getDimensions().w, (f32)getDimensions().h };
	f32 pixelWidth = 1.0f / scaleFactor;
	f32 yMid = area.bottom() / 2;
	f32 yScale = yMid - pixelWidth * 2;

	Color4F waveformColor = Color4(_theme.waveform.r, _theme.waveform.g, _theme.waveform.b, (uint8)(_theme.waveform.a * getAlpha()));
	Color4F waveformSelected = Color4(_theme.waveformSelected.r, _theme.waveformSelected.g, _theme.waveformSelected.b, (uint8)(_theme.waveformSelected.a * getAlpha()));
	Color4F background = Color4(_theme.background.r, _theme.background.g, _theme.background.b, (uint8)(_theme.background.a * getAlpha()));
	Color4F foreground = Color4(_theme.foreground.r, _theme.foreground.g, _theme.foreground.b, (uint8)(_theme.foreground.a * getAlpha()));
	Color4F midLine = Color4(_theme.foreground.r, _theme.foreground.g, _theme.foreground.b, (uint8)(_theme.foreground.a * getAlpha() * 0.65f));

	//nvgStrokeWidth(vg, pixelWidth);

	// Draw background
	canvas.fillRect((Dimension)getDimensions(), background);

	// Draw selection background

	f32 w = getDimensionsF().w;

	f32 selectionStart = 0.0f;
	f32 selectionEnd = w;

	std::vector<WaveFormSection> sections;

	if (_selectionSize) {
		bool selectionStartVisible = sampleToPixel(_selectionOffset, selectionStart);
		bool selectionEndVisible = sampleToPixel(_selectionOffset + _selectionSize, selectionEnd);
		bool selectionVisible = selectionStart < 0 && selectionEnd > w;

		if (selectionVisible) {
			selectionStart = MathUtil::clamp(selectionStart, 0.0f, w);
			selectionEnd = MathUtil::clamp(selectionEnd, 0.0f, w);

			f32 offset = 0.0f;

			if (selectionStartVisible) {
				sections.push_back(WaveFormSection{
					.startPixel = 0,
					.size = selectionStart,
					.color = waveformColor
				});

				offset = selectionStart;
			}

			if (selectionEndVisible) {
				f32 selectedSize = selectionEnd - offset;

				sections.push_back(WaveFormSection{
					.startPixel = offset,
					.size = selectedSize,
					.color = waveformSelected
				});

				sections.push_back(WaveFormSection{
					.startPixel = selectionEnd,
					.size = w - selectionEnd,
					.color = waveformColor
				});
			} else {
				sections.push_back(WaveFormSection{
					.startPixel = offset,
					.size = w - offset,
					.color = waveformSelected
				});
			}
		}
	}

	// Draw outline
	canvas.strokeRect((DimensionF)getDimensions(), foreground);

	if (_waveform.channels.size() == 0) {
		return;
	}

	std::vector<PointF> points = _waveform.channels[0].linePoints;

	canvas.setColor(waveformColor);

	if (points.size() > 2) {
 		// Draw waveform lines

		for (size_t i = 0; i < points.size() - 1; i += 2) {
			points[i].x = (points[i].x + 1.0f) / scaleFactor;
			points[i].y = (points[i].y * yScale) + yMid;

			points[i + 1].x = (points[i + 1].x + 1.0f) / scaleFactor;
			points[i + 1].y = (points[i + 1].y * yScale) + yMid;

			canvas.line(points[i], points[i + 1]);
		}

		if (points.size() < getDimensions().w / 2) {
			// TODO: Draw rects for samples
		}
	}

	// Draw mid line
	canvas.line(0.0f, yMid, area.w, yMid, midLine);
}
