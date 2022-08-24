#include "WaveView.h"

#include <spdlog/spdlog.h>

#include "WaveformUtil.h"

using namespace fw;

const f32 MIN_SAMPLES_VIEWABLE = 100.0f;
const f32 ZOOM_STEP_COUNT = 20.0f;

void WaveView::updateSlice() {
	if (_audioData.size() == 0) {
		return;
	}

	size_t sampleCount = (size_t)(getDimensions().w * getWorldScale()) - 2;
	WaveformUtil::generate(_audioData.slice(_slicePosition, _sliceSize), _waveform, sampleCount);
}

bool WaveView::onMouseScroll(PointT<f32> delta, Point position) {
	// Get the sample under the cursor

	f32 totalSize = (f32)_audioData.size();
	f32 viewableSamples = totalSize * _zoom * _zoom;
	f32 frac = (f32)position.x / (f32)getDimensions().w;

	f32 stepSize = 1.0f / ZOOM_STEP_COUNT;
	_zoom = MathUtil::clamp(_zoom - delta.y * stepSize, 0.01f, 1.0f);

	f32 newViewableSamples = totalSize * _zoom * _zoom;
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

	return true;
}

void WaveView::onRender(Canvas& canvas) {
	f32 scaleFactor = getWorldScale();
	RectF area = { 0, 0, (f32)getDimensions().w, (f32)getDimensions().h };
	f32 pixelWidth = 1.0f / scaleFactor;
	f32 yMid = area.bottom() / 2;
	f32 yScale = yMid - pixelWidth * 2;

	Color4F background = Color4(_theme.background.r, _theme.background.g, _theme.background.b, (uint8)(_theme.background.a * getAlpha()));
	Color4F foreground = Color4(_theme.foreground.r, _theme.foreground.g, _theme.foreground.b, (uint8)(_theme.foreground.a * getAlpha()));
	Color4F midLine = Color4(_theme.foreground.r, _theme.foreground.g, _theme.foreground.b, (uint8)(_theme.foreground.a * getAlpha() * 0.65f));

	//nvgStrokeWidth(vg, pixelWidth);

	// Draw background
	canvas.fillRect((Dimension)getDimensions(), background);

	// Draw outline
	canvas.strokeRect((DimensionF)getDimensions(), foreground);

	// Draw mid line
	canvas.line({ 0, yMid }, { area.w, yMid }, midLine);

	std::vector<PointF> points = _waveform.linePoints;

	if (points.size() > 2) {
 		// Draw waveform lines

		//nvgBeginPath(vg);
		//nvgMoveTo(vg, pixelWidth, points[0].y * yScale + yMid);

		for (size_t i = 0; i < points.size(); ++i) {
			points[i].x = (points[i].x + 1.0f) / scaleFactor;
			points[i].y = (points[i].y * yScale) + yMid;

			//nvgLineTo(vg, (points[i].x + 1.0f) / scaleFactor, points[i].y * yScale + yMid);
		}

		canvas.lines(points, foreground);

		//nvgStrokeColor(vg, foreground);
		//nvgStroke(vg);

		/*if (_markers.size()) {
			// Draw markers
			f32 endOffset = _startOffset + (f32)_sliceSize;
			f32 markerScale = ((f32)getDimensions().w / (f32)_sliceSize);
			NVGcolor selection = nvgRGBA(_theme.selection.r, _theme.selection.g, _theme.selection.b, (uint8)(_theme.selection.a * getAlpha()));

			for (f32 xOff : _markers) {
				if (xOff > _startOffset && xOff < endOffset) {
					f32 pos = (xOff - _startOffset) * markerScale;

					nvgBeginPath(vg);
					nvgMoveTo(vg, pos, pixelWidth);
					nvgLineTo(vg, pos, (f32)getDimensions().h);

					nvgStrokeColor(vg, selection);
					nvgStroke(vg);
				}
			}
		}*/
	}
}
