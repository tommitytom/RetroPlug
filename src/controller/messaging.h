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

struct InstanceSwapDesc {
	InstanceIndex idx;
	SameBoyPlugPtr instance;
};

struct InstanceDuplicateDesc {
	InstanceIndex sourceIdx;
	InstanceIndex targetIdx;
	SameBoyPlugPtr instance;
};

struct VideoBuffer {
	Dimension2 dimensions;
	micromsg::UniquePtr<char> data;
};

struct VideoStream {
	VideoBuffer buffers[MAX_INSTANCES];
};

struct AudioBuffer {
	size_t frameCount = 0;
	size_t channelCount = 0;
	FloatDataBufferPtr data;
};

struct FetchStateRequest {
	SaveStateType type;
	DataBufferPtr buffers[MAX_INSTANCES];
};

struct FetchStateResponse {
	SaveStateType type;
	std::array<size_t, MAX_INSTANCES> sizes = { 0 };
	std::array<DataBufferPtr, MAX_INSTANCES> buffers;
	std::array<std::string, MAX_INSTANCES> components;
};

#define DefinePush(name, arg) class name : public micromsg::Push<arg> {};
#define DefineRequest(name, arg, ret) class name : public micromsg::Request<arg, ret> {};

namespace calls {
	DefinePush(LoadRom, LoadRomDesc);
	DefinePush(TransmitVideo, VideoStream);
	DefinePush(UpdateSettings, Project::Settings);
	DefinePush(PressButtons, ButtonStream<32>);
	DefinePush(ContextMenuResult, int);
	DefinePush(SetActive, InstanceIndex);

	DefineRequest(SwapLuaContext, AudioLuaContextPtr, AudioLuaContextPtr);
	DefineRequest(SwapInstance, InstanceSwapDesc, SameBoyPlugPtr);
	DefineRequest(DuplicateInstance, InstanceDuplicateDesc, SameBoyPlugPtr);
	DefineRequest(TakeInstance, InstanceIndex, SameBoyPlugPtr);
	DefineRequest(FetchState, FetchStateRequest, FetchStateResponse);
}

using Node = micromsg::Node<NodeTypes>;
