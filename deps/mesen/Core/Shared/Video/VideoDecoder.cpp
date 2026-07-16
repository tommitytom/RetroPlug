#include "pch.h"
#include "Shared/Interfaces/IRenderingDevice.h"
#include "Shared/Video/VideoDecoder.h"
#include "Shared/Video/VideoRenderer.h"
#include "Shared/Video/BaseVideoFilter.h"
#include "Shared/NotificationManager.h"
#include "Shared/Emulator.h"
#include "Shared/RewindManager.h"
#include "Shared/EmuSettings.h"
#include "Shared/SettingTypes.h"
#include "Shared/Video/ScaleFilter.h"
#include "Shared/Video/RotateFilter.h"
#include "Shared/Video/ScanlineFilter.h"
#include "Shared/Video/DebugHud.h"
#include "Shared/InputHud.h"
#include "Shared/RenderedFrame.h"
#include "Shared/Video/SystemHud.h"
#include "SNES/CartTypes.h"

VideoDecoder::VideoDecoder(Emulator* emu)
{
	_emu = emu;
	_baseFrameSize = { 256, 239 };
	_lastFrameSize = _baseFrameSize;
}

VideoDecoder::~VideoDecoder()
{
	
}

void VideoDecoder::Init()
{
	UpdateVideoFilter();
	_videoFilter->SetBaseFrameInfo(_baseFrameSize);
}

FrameInfo VideoDecoder::GetBaseFrameInfo(bool removeOverscan)
{
	if(removeOverscan) {
		OverscanDimensions overscan = _emu->GetSettings()->GetOverscan();
		uint32_t hOverscan = overscan.Left + overscan.Right;
		uint32_t vOverscan = overscan.Top + overscan.Bottom;

		bool swapOverscan = (_emu->GetSettings()->GetVideoConfig().ScreenRotation % 180) != 0;
		if(swapOverscan) {
			std::swap(hOverscan, vOverscan);
		}

		return {
			(uint32_t)(_baseFrameSize.Width * _frame.Scale) - hOverscan,
			(uint32_t)(_baseFrameSize.Height * _frame.Scale) - vOverscan
		};
	} else {
		return {
			(uint32_t)(_baseFrameSize.Width * _frame.Scale),
			(uint32_t)(_baseFrameSize.Height * _frame.Scale)
		};
	}
}

FrameInfo VideoDecoder::GetFrameInfo()
{
	return _lastFrameSize;
}

void VideoDecoder::UpdateVideoFilter()
{
	VideoFilterType newFilter = _emu->GetSettings()->GetVideoConfig().VideoFilter;
	ConsoleType consoleType = _emu->GetConsoleType();

	if(_videoFilterType != newFilter || _videoFilter == nullptr || _consoleType != consoleType || _forceFilterUpdate) {
		_videoFilterType = newFilter;
		_consoleType = consoleType;

		_videoFilter.reset(_emu->GetVideoFilter());
		_scaleFilter = ScaleFilter::GetScaleFilter(_emu, _videoFilterType);
		_forceFilterUpdate = false;
	}

	uint32_t screenRotation = _emu->GetSettings()->GetVideoConfig().ScreenRotation;
	_emu->GetScreenRotationOverride(screenRotation);

	if(screenRotation != 0) {
		if(!_rotateFilter || _rotateFilter->GetAngle() != screenRotation) {
			_rotateFilter.reset(new RotateFilter(screenRotation));
		}
	} else {
		_rotateFilter.reset();
	}
}

void VideoDecoder::DecodeFrame(bool forRewind)
{
	UpdateVideoFilter();

	bool isAudioPlayer = _emu->GetAudioPlayerHud() != nullptr;
	if(isAudioPlayer) {
		//When an audio file is loaded, force base resolution to 256x240 for all consoles
		_baseFrameSize.Width = 256;
		_baseFrameSize.Height = 240;
	} else {
		_baseFrameSize.Width = _frame.Width;
		_baseFrameSize.Height = _frame.Height;
	}

	_videoFilter->SetBaseFrameInfo(_baseFrameSize);
	FrameInfo frameSize = _videoFilter->SendFrame((uint16_t*)_frame.FrameBuffer, _frame.FrameNumber, _frame.VideoPhase, _frame.Data);

	uint32_t* outputBuffer = _videoFilter->GetOutputBuffer();
	
	OverscanDimensions overscan = _videoFilter->GetOverscan();

	if(_rotateFilter && !isAudioPlayer) {
		outputBuffer = _rotateFilter->ApplyFilter(outputBuffer, frameSize.Width, frameSize.Height);
		if((_rotateFilter->GetAngle() % 180) != 0) {
			//90 or 270 rotation, swap height & width
			std::swap(_baseFrameSize.Width, _baseFrameSize.Height);
			frameSize = _rotateFilter->GetFrameInfo(frameSize);
		}
	}

	_emu->GetDebugHud()->Draw(outputBuffer, frameSize, overscan, _frame.FrameNumber, _videoFilter->GetScaleFactor());

	if(_scaleFilter && !isAudioPlayer) {
		outputBuffer = _scaleFilter->ApplyFilter(outputBuffer, frameSize.Width, frameSize.Height);
		frameSize = _scaleFilter->GetFrameInfo(frameSize);
	}

	if(!isAudioPlayer) {
		uint8_t scale = std::max<uint8_t>(1, (uint8_t)((double)frameSize.Height / (_frame.Height - overscan.Top - overscan.Bottom)));
		ScanlineFilter::ApplyFilter(outputBuffer, frameSize.Width, frameSize.Height, _emu->GetSettings()->GetVideoConfig().ScanlineIntensity, scale);
	}

	RenderedFrame convertedFrame((void*)outputBuffer, frameSize.Width, frameSize.Height, _frame.Scale, _frame.FrameNumber, _frame.InputData);

	double aspectRatio = _emu->GetSettings()->GetAspectRatio(_emu->GetRegion(), _baseFrameSize);
	if(frameSize.Height != _lastFrameSize.Height || frameSize.Width != _lastFrameSize.Width || aspectRatio != _lastAspectRatio) {
		_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::ResolutionChanged);
	}
	_lastAspectRatio = aspectRatio;
	_lastFrameSize = frameSize;
	
	//Rewind manager will take care of sending the correct frame to the video renderer
	_emu->GetRewindManager()->SendFrame(convertedFrame, forRewind);
}

uint32_t VideoDecoder::GetFrameCount()
{
	return _frameCount;
}

void VideoDecoder::WaitForAsyncFrameDecode()
{
	//while(_frameChanged) {
		//Spin until decode is done
		//std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(15));
	//}
}

void VideoDecoder::UpdateFrame(RenderedFrame frame, bool sync, bool forRewind)
{
	if(_emu->IsRunAheadFrame()) {
		return;
	}

	_emu->OnBeforeSendFrame();

	_frame = frame;
	DecodeFrame(forRewind);
	_frameCount++;
}

void VideoDecoder::StartThread()
{
	_videoFilter.reset();
	UpdateVideoFilter();
	_videoFilter->SetBaseFrameInfo(_baseFrameSize);
	_frameCount = 0;

	_emu->GetVideoRenderer()->ClearFrame();
}

void VideoDecoder::StopThread()
{
	_emu->GetVideoRenderer()->ClearFrame();
}

bool VideoDecoder::IsRunning()
{
	return true;
}

void VideoDecoder::TakeScreenshot()
{
	if(_videoFilter) {
		_videoFilter->TakeScreenshot(_emu->GetRomInfo().RomFile.GetFileName(), _videoFilterType);
	}
}

void VideoDecoder::TakeScreenshot(std::stringstream &stream)
{
	if(_videoFilter) {
		_videoFilter->TakeScreenshot(_videoFilterType, "", &stream);
	}
}
