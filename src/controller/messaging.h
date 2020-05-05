#pragma once

#include "micromsg/request.h"
#include "micromsg/node.h"
#include "model/Project.h"
#include "model/ButtonStream.h"

class SameBoyPlug;
using SameBoyPlugPtr = std::shared_ptr<SameBoyPlug>;

class AudioLuaContext;
using AudioLuaContextPtr = std::shared_ptr<AudioLuaContext>;

enum class NodeTypes {
	Ui,
	Audio,

	COUNT
};

struct LoadRomDesc {
	// romData
	bool reset = true;
};

struct SystemSwapDesc {
	SystemIndex idx;
	SameBoyPlugPtr instance;
	std::shared_ptr<std::string> componentState;
};

struct SystemDuplicateDesc {
	SystemIndex sourceIdx;
	SystemIndex targetIdx;
	SameBoyPlugPtr instance;
};

struct VideoBuffer {
	Dimension2 dimensions;
	micromsg::UniquePtr<char> data;
};

struct VideoStream {
	VideoBuffer buffers[MAX_SYSTEMS];
};

struct AudioBuffer {
	size_t frameCount = 0;
	size_t channelCount = 0;
	FloatDataBufferPtr data;
};

struct FetchStateRequest {
	SaveStateType type;
	DataBufferPtr buffers[MAX_SYSTEMS];
};

struct FetchStateResponse {
	SaveStateType type;
	std::array<size_t, MAX_SYSTEMS> sizes = { 0 };
	std::array<DataBufferPtr, MAX_SYSTEMS> buffers;
	std::array<std::string, MAX_SYSTEMS> components;
};

struct ResetSystemDesc {
	SystemIndex idx;
	GameboyModel model;
};

struct FetchSramRequest {
	SystemIndex idx;
	DataBufferPtr buffer;
};

struct SetDataRequest {
	SystemIndex idx;
	DataBufferPtr buffer;
	bool reset;
};

#define DefinePush(name, arg) class name : public micromsg::Push<arg> {};
#define DefineRequest(name, arg, ret) class name : public micromsg::Request<arg, ret> {};

namespace calls {
	DefinePush(LoadRom, LoadRomDesc);
	DefinePush(TransmitVideo, VideoStream);
	DefinePush(UpdateSettings, Project::Settings);
	DefinePush(PressButtons, ButtonStream<32>);
	DefinePush(ContextMenuResult, int);
	DefinePush(SetActive, SystemIndex);
	DefinePush(ResetSystem, ResetSystemDesc);

	DefineRequest(SwapLuaContext, AudioLuaContextPtr, AudioLuaContextPtr);
	DefineRequest(SwapSystem, SystemSwapDesc, SystemSwapDesc);
	DefineRequest(SetSram, SetDataRequest, DataBufferPtr);
	DefineRequest(SetRom, SetDataRequest, DataBufferPtr);
	DefineRequest(DuplicateSystem, SystemDuplicateDesc, SameBoyPlugPtr);
	DefineRequest(TakeSystem, SystemIndex, SameBoyPlugPtr);
	DefineRequest(FetchState, FetchStateRequest, FetchStateResponse);

	DefineRequest(FetchSram, FetchSramRequest, DataBufferPtr);
}

using Node = micromsg::Node<NodeTypes>;
