#pragma once

/*#ifdef FW_USE_GLFW
#include "application/GlfwNativeWindow.h"
namespace orb::app { using WindowManagerT = GlfwWindowManager; }
#else
#include "application/WindowManager.h"
namespace orb::app { using WindowManagerT = WindowManager; }
#endif*/

#ifdef FW_USE_MINIAUDIO
#include "audio/MiniAudioManager.h"
namespace orb::app { using AudioManagerT = audio::MiniAudioManager; }
#else
#include "audio/AudioManager.h"
namespace orb::app { using AudioManagerT = audio::AudioManager; }
#endif
