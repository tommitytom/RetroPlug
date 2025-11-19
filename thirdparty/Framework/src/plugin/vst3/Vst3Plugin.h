#pragma once

#include "entry/ApplicationFactory.h"

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/vst/ivstcontextmenu.h"
#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

namespace fw {
	class Vst3Plugin : public Steinberg::Vst::SingleComponentEffect, public Steinberg::Vst::IMidiMapping, public Steinberg::Vst::ChannelContext::IInfoListener {
	private:
		std::unique_ptr<orb::app::Application> _app;
		orb::AudioProcessorPtr _processor;

	public:
		Vst3Plugin(std::unique_ptr<orb::app::Application> app) : _app(std::move(app)) {}
		~Vst3Plugin() = default;

		// AudioEffect
		Steinberg::tresult PLUGIN_API initialize(FUnknown* context) override;
		Steinberg::tresult PLUGIN_API terminate() override;
		Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* pInputs, Steinberg::int32 numIns, Steinberg::Vst::SpeakerArrangement* pOutputs, Steinberg::int32 numOuts) override;
		Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
		Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) override;
		Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) override;
		Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
		Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
		Steinberg::uint32 PLUGIN_API getLatencySamples() override { return 0; }
		Steinberg::uint32 PLUGIN_API getTailSamples() override { return 0; }
		Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* pState) override;
		Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* pState) override;

		// IEditController
		Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized(Steinberg::Vst::ParamID tag) override;
		Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) override;
		Steinberg::IPlugView* PLUGIN_API createView(const char* name) override;
		Steinberg::tresult PLUGIN_API setEditorState(Steinberg::IBStream* pState) override;
		Steinberg::tresult PLUGIN_API getEditorState(Steinberg::IBStream* pState) override;
		Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;

		// IMidiMapping
		Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel, Steinberg::Vst::CtrlNumber midiCCNumber, Steinberg::Vst::ParamID& tag) override;

		// IUnitInfo
		Steinberg::tresult PLUGIN_API getProgramName(Steinberg::Vst::ProgramListID listId, Steinberg::int32 programIndex, Steinberg::Vst::String128 name /*out*/) override
		{
			return 0;
		}

		Steinberg::int32 PLUGIN_API getProgramListCount() override
		{
			return 0;
		}

		Steinberg::tresult PLUGIN_API getProgramListInfo(Steinberg::int32 listIndex, Steinberg::Vst::ProgramListInfo& info) override
		{
			return 0;
		}

		// IInfoListener
		Steinberg::tresult PLUGIN_API setChannelContextInfos(Steinberg::Vst::IAttributeList* list) override;

		Steinberg::Vst::IComponentHandler* GetComponentHandler() { return componentHandler; }

		Steinberg::Vst::AudioBus* getAudioInput(Steinberg::int32 index)
		{
			Steinberg::Vst::AudioBus* bus = nullptr;
			if (index < static_cast<Steinberg::int32>(audioInputs.size()))
				bus = Steinberg::FCast<Steinberg::Vst::AudioBus>(audioInputs.at(index));
			return bus;
		}

		Steinberg::Vst::AudioBus* getAudioOutput(Steinberg::int32 index)
		{
			Steinberg::Vst::AudioBus* bus = nullptr;
			if (index < static_cast<Steinberg::int32>(audioOutputs.size()))
				bus = Steinberg::FCast<Steinberg::Vst::AudioBus>(audioOutputs.at(index));
			return bus;
		}

		// Interface
		OBJ_METHODS(Vst3Plugin, SingleComponentEffect)
			DEFINE_INTERFACES
			DEF_INTERFACE(IMidiMapping)
			DEF_INTERFACE(IInfoListener)
			END_DEFINE_INTERFACES(SingleComponentEffect)
			REFCOUNT_METHODS(SingleComponentEffect)
	};
}
