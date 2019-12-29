#pragma once

#include "micromsg/request.h"
#include "micromsg/node.h"
#include "model/Project.h"

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

namespace calls {
	using LoadRom = micromsg::Push<LoadRomDesc>;
	using TransmitVideo = micromsg::Push<VideoStream>;
	using UpdateSettings = micromsg::Push<Project::Settings>;

	using SwapInstance = micromsg::Request<InstanceSwapDesc, SameBoyPlugPtr>;
	using TakeInstance = micromsg::Request<InstanceIndex, SameBoyPlugPtr>;
}

using Node = micromsg::Node<NodeTypes>;