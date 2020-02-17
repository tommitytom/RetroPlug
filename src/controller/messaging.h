#pragma once

#include "micromsg/request.h"
#include "micromsg/node.h"
#include "model/Project.h"
#include "model/ButtonStream.h"

class SameBoyPlug;
using SameBoyPlugPtr = std::shared_ptr<SameBoyPlug>;

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
};

namespace calls {
	using LoadRom = micromsg::Push<LoadRomDesc>;
	using TransmitVideo = micromsg::Push<VideoStream>;
	using UpdateSettings = micromsg::Push<Project::Settings>;
	using PressButtons = micromsg::Push<ButtonStream<32>>;
	using ContextMenuResult = micromsg::Push<int>;

	using SwapInstance = micromsg::Request<InstanceSwapDesc, SameBoyPlugPtr>;
	using DuplicateInstance = micromsg::Request<InstanceDuplicateDesc, SameBoyPlugPtr>;
	using TakeInstance = micromsg::Request<InstanceIndex, SameBoyPlugPtr>;
	using FetchState = micromsg::Request<FetchStateRequest, FetchStateResponse>;
}

using Node = micromsg::Node<NodeTypes>;
