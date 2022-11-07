#pragma once

#ifdef FW_USE_GLFW
#include "application/GlfwNativeWindow.h"
namespace fw::app { using WindowManagerT = GlfwWindowManager; }
#else
#include "application/WindowManager.h"
namespace fw::app { using WindowManagerT = WindowManager; }
#endif

#ifdef FW_USE_MINIAUDIO
#include "audio/MiniAudioManager.h"
namespace fw::app { using AudioManagerT = audio::MiniAudioManager; }
#else
#include "audio/AudioManager.h"
namespace fw::app { using AudioManagerT = audio::AudioManager; }
#endif
