#include "Vst3Plugin.h"

#include "Vst3PluginView.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace fw {
	using namespace Steinberg;
	using namespace Vst;

	tresult PLUGIN_API Vst3Plugin::initialize(FUnknown* context)
	{
		if (SingleComponentEffect::initialize(context) == kResultOk) {
			_processor = _app->onCreateAudio();
			return kResultOk;
		}

		return kResultFalse;
	}

	tresult PLUGIN_API Vst3Plugin::terminate() {
		return SingleComponentEffect::terminate();
	}

	tresult PLUGIN_API Vst3Plugin::setBusArrangements(SpeakerArrangement* pInputBusArrangements, int32 numInBuses, SpeakerArrangement* pOutputBusArrangements, int32 numOutBuses) {
		return kResultFalse;
		//return Vst3PluginProcessorBase::SetBusArrangements(this, pInputBusArrangements, numInBuses, pOutputBusArrangements, numOutBuses) ? kResultTrue : kResultFalse;
	}

	tresult PLUGIN_API Vst3Plugin::setActive(TBool state) {
		//_processor->setActive((bool)state));
		return SingleComponentEffect::setActive(state);
	}

	tresult PLUGIN_API Vst3Plugin::setupProcessing(ProcessSetup& newSetup) {
		return kResultFalse;
		//return SetupProcessing(newSetup, processSetup) ? kResultOk : kResultFalse;
	}

	tresult PLUGIN_API Vst3Plugin::setProcessing(TBool state) {
		return kResultFalse;
		//return SetProcessing((bool)state) ? kResultOk : kResultFalse;
	}

	tresult PLUGIN_API Vst3Plugin::process(ProcessData& data) {
		//_processor->onRender();
		//Process(data, processSetup, audioInputs, audioOutputs, mMidiMsgsFromEditor, mMidiMsgsFromProcessor, mSysExDataFromEditor, mSysexBuf);
		return kResultOk;
	}

	tresult PLUGIN_API Vst3Plugin::canProcessSampleSize(int32 symbolicSampleSize) {
		return kResultFalse;
		//return CanProcessSampleSize(symbolicSampleSize) ? kResultTrue : kResultFalse;
	}

	tresult PLUGIN_API Vst3Plugin::setState(IBStream* pState) {
		return kResultFalse;
		//return Vst3PluginState::SetState(this, pState) ? kResultOk : kResultFalse;
	}

	tresult PLUGIN_API Vst3Plugin::getState(IBStream* pState) {
		return kResultFalse;
		//return Vst3PluginState::GetState(this, pState) ? kResultOk : kResultFalse;
	}

	ParamValue PLUGIN_API Vst3Plugin::getParamNormalized(ParamID tag) {
		return 0.0;
		//return Vst3PluginControllerBase::GetParamNormalized(tag);
	}

	tresult PLUGIN_API Vst3Plugin::setParamNormalized(ParamID tag, ParamValue value) {
		return kResultFalse;
		/*if (Vst3PluginControllerBase::SetParamNormalized(this, tag, value)) {
			return kResultTrue;
		} else {
			return kResultFalse;
		}*/
	}

	IPlugView* PLUGIN_API Vst3Plugin::createView(const char* name) {
		if (name && strcmp(name, "editor") == 0) {
			return new Vst3PluginView(_app->onCreateUi());
		}

		return 0;
	}

	tresult PLUGIN_API Vst3Plugin::setEditorState(IBStream* pState) {
		// Currently nothing to do here
		return kResultOk;
	}

	tresult PLUGIN_API Vst3Plugin::getEditorState(IBStream* pState) {
		// Currently nothing to do here
		return kResultOk;
	}

	tresult PLUGIN_API Vst3Plugin::setComponentState(IBStream* pState) {
		// We get the state through setState so do nothing here
		return kResultOk;
	}

	tresult PLUGIN_API Vst3Plugin::getMidiControllerAssignment(int32 busIndex, int16 midiChannel, CtrlNumber midiCCNumber, ParamID& tag) {
		return kResultFalse;
		/*
		if (busIndex == 0 && midiChannel < VST3_NUM_CC_CHANS) {
			tag = kMIDICCParamStartIdx + (midiChannel * kCountCtrlNumber) + midiCCNumber;
			return kResultTrue;
		}

		return kResultFalse;*/
	}

	Steinberg::tresult PLUGIN_API Vst3Plugin::setChannelContextInfos(Steinberg::Vst::IAttributeList* pList) {
		return kResultFalse;
		//return Vst3PluginControllerBase::SetChannelContextInfos(pList) ? kResultTrue : kResultFalse;
	}
}

bool InitModule() {
#ifdef FW_OS_WIN
	extern void* moduleHandle;
	gHINSTANCE = (HINSTANCE)moduleHandle;
#endif
	return true;
}

// called after library is unloaded
bool DeinitModule() {
	return true;
}

static Steinberg::FUnknown* createInstance(void*) {
	return (Steinberg::Vst::IAudioProcessor*)new fw::Vst3Plugin(fw::ApplicationFactory::create());
}

#define PLUG_MFR "AcmeInc"
#define PLUG_MFR_ID 'Acme'
#define PLUG_UNIQUE_ID 'PmBl'
#define PLUG_URL_STR "https://iplug2.github.io"
#define PLUG_EMAIL_STR "spam@me.com"
#define PLUG_VERSION_STR "1.0.0"
#define VST3_SUBCATEGORY "Instrument|Synth"
#define PLUG_NAME "IPlugInstrument"
#define VST3_PROCESSOR_UID 0xF2AEE70D, 0x00DE4F4E, PLUG_MFR_ID, PLUG_UNIQUE_ID
#define EFFECT_TYPE_VST3 kInstrumentSynth

BEGIN_FACTORY_DEF(PLUG_MFR, PLUG_URL_STR, PLUG_EMAIL_STR)

DEF_CLASS2(INLINE_UID_FROM_FUID(FUID(VST3_PROCESSOR_UID)),
	Steinberg::PClassInfo::kManyInstances,          // cardinality
	kVstAudioEffectClass,                           // the component category (don't change this)
	PLUG_NAME,                                      // plug-in name
	Steinberg::Vst::kSimpleModeSupported,           // means gui and plugin aren't split
	VST3_SUBCATEGORY,                               // Subcategory for this plug-in
	PLUG_VERSION_STR,                               // plug-in version
	kVstVersionString,                              // the VST 3 SDK version (don't change - use define)
	createInstance)                                 // function pointer called to be instantiate
END_FACTORY
