#include "SystemView.h"

#include <sstream>

#include "platform/FileDialog.h"
#include "platform/Path.h"
#include "platform/Shell.h"
#include "util/File.h"
#include "Buttons.h"

SystemView::SystemView(SystemIndex idx, IGraphics* graphics)
	: _index(idx), _graphics(graphics)
{
	for (size_t i = 0; i < 2; i++) {
		_textIds[i] = new ITextControl(IRECT(0, -100, 0, 0), "", IText(23, COLOR_WHITE));
		graphics->AttachControl(_textIds[i]);
	}
}

SystemView::~SystemView() {
	HideText();
	DeleteFrame();
}

void SystemView::DeleteFrame() {
	if (_imageId != -1) {
		NVGcontext* ctx = (NVGcontext*)_graphics->GetDrawContext();
		nvgDeleteImage(ctx, _imageId);
		nvgBeginPath(ctx);
		nvgRect(ctx, _area.L, _area.T, _area.W(), _area.H());
		NVGcolor black = { 0, 0, 0, 1 };
		nvgFillColor(ctx, black);
		nvgFill(ctx);

		_imageId = -1;
	}

	if (_frameBuffer) {
		delete[] _frameBuffer;
		_frameBuffer = nullptr;
		_frameBufferSize = 0;
	}
}

void SystemView::WriteFrame(const VideoBuffer& buffer) {
	const char* frameData = buffer.data.get();
	if (frameData) {
		if (buffer.data.count() > _frameBufferSize) {
			if (_frameBuffer) {
				delete[] _frameBuffer;
			}

			_dimensions = buffer.dimensions;

			_frameBufferSize = buffer.data.count();
			_frameBuffer = new char[_frameBufferSize];

			if (_imageId != -1) {
				NVGcontext* ctx = (NVGcontext*)_graphics->GetDrawContext();
				nvgDeleteImage(ctx, _imageId);
				_imageId = -1;
			}
		}

		memcpy(_frameBuffer, frameData, _frameBufferSize);
		_frameDirty = true;
	}
}

void SystemView::ShowText(const std::string & row1, const std::string & row2) {
	_showText = true;
	_textIds[0]->SetStr(row1.c_str());
	_textIds[1]->SetStr(row2.c_str());
	UpdateTextPosition();
}

void SystemView::HideText() {
	_showText = false;
	UpdateTextPosition();
}

void SystemView::UpdateTextPosition() {
	if (_showText) {
		float mid = _area.H() / 2;
		IRECT topRow(_area.L, mid - 25, _area.R, mid);
		IRECT bottomRow(_area.L, mid, _area.R, mid + 25);
		_textIds[0]->SetTargetAndDrawRECTs(topRow);
		_textIds[1]->SetTargetAndDrawRECTs(bottomRow);
	} else {
		_textIds[0]->SetTargetAndDrawRECTs(IRECT(0, -100, 0, 0));
		_textIds[1]->SetTargetAndDrawRECTs(IRECT(0, -100, 0, 0));
	}
}

void SystemView::SetArea(const IRECT & area) {
	_area = area;
	UpdateTextPosition();
}

void SystemView::Draw(IGraphics& g, double delta) {
	NVGcontext* vg = (NVGcontext*)g.GetDrawContext();
	if (_index != NO_ACTIVE_SYSTEM) {
		DrawPixelBuffer(vg);
	}
}

void SystemView::DrawPixelBuffer(NVGcontext* vg) {
	if (_frameDirty && _frameBuffer) {
		if (_imageId == -1) {
			_imageId = nvgCreateImageRGBA(vg, _dimensions.w, _dimensions.h, NVG_IMAGE_NEAREST, (const unsigned char*)_frameBuffer);
		} else {
			nvgUpdateImage(vg, _imageId, (const unsigned char*)_frameBuffer);
		}

		_frameDirty = false;
	}

	if (_imageId != -1) {
		nvgBeginPath(vg);

		NVGpaint imgPaint = nvgImagePattern(vg, _area.L, _area.T, (float)(_dimensions.w * _zoom), (float)(_dimensions.h * _zoom), 0, _imageId, _alpha);
		nvgRect(vg, _area.L, _area.T, _area.W(), _area.H());
		nvgFillPaint(vg, imgPaint);
		nvgFill(vg);
	}
}
