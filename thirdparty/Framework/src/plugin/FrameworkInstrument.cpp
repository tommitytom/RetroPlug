#include "FrameworkInstrument.h"

#include "IPlug_include_in_plug_src.h"
#include "IGraphics_include_in_plug_src.h"

#include "FrameworkView.h"

FrameworkInstrument::FrameworkInstrument(const InstanceInfo& info) : Plugin(info, MakeConfig(0, 0)) {
#if IPLUG_EDITOR 
    mMakeGraphicsFunc = [&]() {
        return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
    };

    mLayoutFunc = [&](IGraphics* pGraphics) {
        pGraphics->AttachControl(new FrameworkView(IRECT(0, 0, PLUG_WIDTH, PLUG_HEIGHT)));
    };
#endif
}
