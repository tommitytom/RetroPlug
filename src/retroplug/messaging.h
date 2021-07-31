#pragma once

#include "micromsg/request.h"
#include "micromsg/node.h"
#include "Messages.h"

#define DefinePush(name, arg) class name : public micromsg::Push<arg> {};
#define DefineRequest(name, arg, ret) class name : public micromsg::Request<arg, ret> {};

namespace calls {
	DefinePush(LoadRom, LoadRomDesc);
	DefinePush(TransmitVideo, VideoStream);
	DefinePush(UpdateProjectSettings, Project::Settings);
	DefinePush(UpdateSystemSettings, SystemSettings);
	DefinePush(PressButtons, ButtonPressState);
	DefinePush(ContextMenuResult, int);
	DefinePush(SetActive, SystemIndex);
	DefinePush(ResetSystem, ResetSystemDesc);
	DefinePush(EnableRendering, bool);
	DefinePush(SramChanged, SetDataRequest);

	DefineRequest(SwapLuaContext, AudioLuaContextPtr, AudioLuaContextPtr);
	DefineRequest(SwapSystem, SystemSwapDesc, SystemSwapDesc);
	DefineRequest(SetRom, SetDataRequest, DataBufferPtr);
	DefineRequest(SetSram, SetDataRequest, DataBufferPtr);
	DefineRequest(SetState, SetDataRequest, DataBufferPtr);
	DefineRequest(DuplicateSystem, SystemDuplicateDesc, SameBoyPlugPtr);
	DefineRequest(TakeSystem, SystemIndex, SameBoyPlugPtr);
	DefineRequest(FetchState, FetchStateRequest, FetchStateResponse);

	DefineRequest(FetchSram, FetchSramRequest, DataBufferPtr);
}

using Node = micromsg::Node<NodeTypes>;
