#pragma once

#include "util/MethodProxy.h"

namespace rp {
	class AudioState;
	class UiState;

	using AudioContextProxy = MethodProxy::Proxy<AudioState>;
	using UiContextProxy = MethodProxy::Proxy<UiState>;
}
