#pragma once
#include "pch.h"
#include "Utilities/SimpleLock.h"
#include "Utilities/AutoResetEvent.h"
#include "Shared/SettingTypes.h"
#include "Shared/RenderedFrame.h"

class BaseVideoFilter;
class ScaleFilter;
class RotateFilter;
class IRenderingDevice;
class Emulator;

class VideoDecoder
{
private:
	Emulator* _emu;

	ConsoleType _consoleType = ConsoleType::Snes;

	uint32_t _frameCount = 0;
	bool _forceFilterUpdate = false;

	double _lastAspectRatio = 0.0;

	FrameInfo _baseFrameSize = {};
	FrameInfo _lastFrameSize = {};
	RenderedFrame _frame = {};
	// RetroPlug: the decoded frame DecodeFrame hands on, refilled per frame for the same reason _frame is kept (its
	// input snapshot's storage survives from frame to frame).
	RenderedFrame _convertedFrame = {};

	VideoFilterType _videoFilterType = VideoFilterType::None;
	unique_ptr<BaseVideoFilter> _videoFilter;
	unique_ptr<ScaleFilter> _scaleFilter;
	unique_ptr<RotateFilter> _rotateFilter;

	void UpdateVideoFilter();

public:
	VideoDecoder(Emulator* console);
	~VideoDecoder();

	void Init();

	// RetroPlug: size everything the first decoded frame will use, without decoding or emitting a frame: the two
	// persistent frames' input snapshots, the video filter's output buffer (at the size the console's PPU emits) and
	// the renderer's last-frame copy. A core calls this at activation, on the thread that builds it, so its first block
	// does not allocate Mesen's frame plumbing on the block thread. Nothing a frame shows or carries changes: no
	// notification, no frame count, no renderer call.
	void WarmUp(uint32_t width, uint32_t height, const vector<ControllerData>& inputData);

	void DecodeFrame(bool synchronous = false);
	void TakeScreenshot();
	void TakeScreenshot(std::stringstream &stream);
	
	void ForceFilterUpdate() { _forceFilterUpdate = true; }

	uint32_t GetFrameCount();
	FrameInfo GetBaseFrameInfo(bool removeOverscan);
	FrameInfo GetFrameInfo();
	double GetLastFrameScale() { return _frame.Scale; }

	void UpdateFrame(const RenderedFrame& frame, bool sync, bool forRewind);

	void WaitForAsyncFrameDecode();

	bool IsRunning();
	void StartThread();
	void StopThread();
};
