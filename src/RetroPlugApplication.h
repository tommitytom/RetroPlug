#pragma once

//#include <entt/meta/container.hpp>

#include "core/Project.h"
#include "core/ProxySystem.h"
#include "core/RetroPlugProcessor.h"

#include "ui/RetroPlugView.h"
#include "ui/ViewLayout.h"

#include "application/Application.h"

#include "sameboy/SameBoyFactory.h"
#include "lsdj/LsdjServiceProvider.h"
#include "core/MgbService.h"

using namespace rp;

class RetroPlugApplication : public fw::app::Application {
private:
	IoMessageBus _ioMessageBus;
	fw::TypeRegistry _typeRegistry;
	SystemFactory _systemFactory;

public:
	RetroPlugApplication() {
		_typeRegistry.addCommonTypes();

		_typeRegistry.addType<fw::TypeId>();
		_typeRegistry.addType<entt::any>();
		_typeRegistry.addEnum<AudioChannelRouting>();
		_typeRegistry.addEnum<MidiChannelRouting>();
		_typeRegistry.addEnum<SystemLayout>();
		_typeRegistry.addEnum<SaveStateType>();

		_typeRegistry.addEnum<fw::FlexUnit>();

		_typeRegistry.addEnum<fw::CursorType>();
		_typeRegistry.addEnum<fw::FlexAlign>();
		_typeRegistry.addEnum<fw::FlexDimension>();
		_typeRegistry.addEnum<fw::LayoutDirection>();
		_typeRegistry.addEnum<fw::FlexDisplay>();
		_typeRegistry.addEnum<fw::FlexEdge>();
		_typeRegistry.addEnum<fw::FlexDirection>();
		_typeRegistry.addEnum<fw::FlexGutter>();
		_typeRegistry.addEnum<fw::FlexJustify>();
		_typeRegistry.addEnum<fw::FlexMeasureMode>();
		_typeRegistry.addEnum<fw::FlexNodeType>();
		_typeRegistry.addEnum<fw::FlexOverflow>();
		_typeRegistry.addEnum<fw::FlexPositionType>();
		_typeRegistry.addEnum<fw::FlexWrap>();
		
		_typeRegistry.addType<fw::FlexValue>()
			.addProperty<&fw::FlexValue::getUnit, &fw::FlexValue::setUnit>("unit")
			.addProperty<&fw::FlexValue::getValue, &fw::FlexValue::setValue>("value")
			;

		_typeRegistry.addType<fw::FlexRect>()
			.addField<&fw::FlexRect::top>("top")
			.addField<&fw::FlexRect::left>("left")
			.addField<&fw::FlexRect::bottom>("bottom")
			.addField<&fw::FlexRect::right>("right")
			;

		_typeRegistry.addType<fw::FlexBorder>()
			.addField<&fw::FlexBorder::top>("top")
			.addField<&fw::FlexBorder::left>("left")
			.addField<&fw::FlexBorder::bottom>("bottom")
			.addField<&fw::FlexBorder::right>("right")
			;

		_typeRegistry.addType<fw::ViewLayout>()
			.addProperty<&fw::ViewLayout::getFlexDirection, &fw::ViewLayout::setFlexDirection>("flexDirection")
			.addProperty<&fw::ViewLayout::getJustifyContent, &fw::ViewLayout::setJustifyContent>("justifyContent")
			.addProperty<&fw::ViewLayout::getFlexAlignItems, &fw::ViewLayout::setFlexAlignItems>("flexAlignItems")
			.addProperty<&fw::ViewLayout::getFlexAlignSelf, &fw::ViewLayout::setFlexAlignSelf>("flexAlignSelf")
			.addProperty<&fw::ViewLayout::getFlexAlignContent, &fw::ViewLayout::setFlexAlignContent>("flexAlignContent")
			.addProperty<&fw::ViewLayout::getLayoutDirection, &fw::ViewLayout::setLayoutDirection>("layoutDirection")
			.addProperty<&fw::ViewLayout::getFlexWrap, &fw::ViewLayout::setFlexWrap>("flexWrap")
			.addProperty<&fw::ViewLayout::getFlexGrow, &fw::ViewLayout::setFlexGrow>("flexGrow")
			.addProperty<&fw::ViewLayout::getFlexShrink, &fw::ViewLayout::setFlexShrink>("flexShrink")
			.addProperty<&fw::ViewLayout::getFlexBasis, &fw::ViewLayout::setFlexBasis>("flexBasis")
			.addProperty<&fw::ViewLayout::getMinWidth, &fw::ViewLayout::setMinWidth>("minWidth")
			.addProperty<&fw::ViewLayout::getMaxWidth, &fw::ViewLayout::setMaxWidth>("maxWidth")
			.addProperty<&fw::ViewLayout::getMinHeight, &fw::ViewLayout::setMinHeight>("minHeight")
			.addProperty<&fw::ViewLayout::getMaxHeight, &fw::ViewLayout::setMaxHeight>("maxHeight")
			.addProperty<&fw::ViewLayout::getWidth, &fw::ViewLayout::setWidth>("width")
			.addProperty<&fw::ViewLayout::getHeight, &fw::ViewLayout::setHeight>("height")
			.addProperty<&fw::ViewLayout::getAspectRatio, &fw::ViewLayout::setAspectRatio>("aspectRatio")
			.addProperty<&fw::ViewLayout::getPosition, &fw::ViewLayout::setPosition>("position")
			.addProperty<&fw::ViewLayout::getPadding, &fw::ViewLayout::setPadding>("padding")
			.addProperty<&fw::ViewLayout::getMargin, &fw::ViewLayout::setMargin>("margin")
			.addProperty<&fw::ViewLayout::getBorder, &fw::ViewLayout::setBorder>("border")
			.addProperty<&fw::ViewLayout::getOverflow, &fw::ViewLayout::setOverflow>("overflow")
			;

		_typeRegistry.addType<ProjectState::Settings>()
			.addField<&ProjectState::Settings::audioRouting>("audioRouting")
			.addField<&ProjectState::Settings::autoSave>("autoSave")
			.addField<&ProjectState::Settings::includeRom>("includeRom")
			.addField<&ProjectState::Settings::layout>("layout")
			.addField<&ProjectState::Settings::midiRouting>("midiRouting")
			.addField<&ProjectState::Settings::saveType>("saveType")
			.addField<&ProjectState::Settings::zoom>("zoom");

		_typeRegistry.addType<SystemPaths>()
			.addField<&SystemPaths::romPath>("romPath")
			.addField<&SystemPaths::sramPath>("sramPath");		

		_typeRegistry.addType<ProjectState>()
			.addField<&ProjectState::settings>("settings")
			//.addField<&ProjectState::path>("path")
			;

		_typeRegistry.addType<SystemSettings::InputSettings>()
			.addField<&SystemSettings::InputSettings::key>("key")
			.addField<&SystemSettings::InputSettings::pad>("pad");

		_typeRegistry.addType<SystemSettings>()
			.addField<&SystemSettings::includeRom>("includeRom")
			.addField<&SystemSettings::gameLink>("gameLink")
			.addField<&SystemSettings::input>("input");

		_typeRegistry.addType<std::unordered_map<SystemServiceType, entt::any>>();

		_typeRegistry.addType<SystemDesc>()
			.addField<&SystemDesc::paths>("paths")
			.addField<&SystemDesc::services>("services")
			.addField<&SystemDesc::settings>("settings")
			;

		_typeRegistry.addType<std::vector<SystemDesc>>();

		_typeRegistry.addType<GlobalConfig>()
			.addField<&GlobalConfig::projectSettings>("project")
			.addField<&GlobalConfig::systemSettings>("system");

		_typeRegistry.addEnum<LsdjSyncMode>();

		_typeRegistry.addType<ArduinoboyServiceSettings>()
			.addField<&ArduinoboyServiceSettings::syncMode>("syncMode")
			.addField<&ArduinoboyServiceSettings::tempoDivisor>("tempoDivisor")
			;

		_systemFactory.addSystemProvider<SameBoyProvider>();
		_systemFactory.addSystemProvider<ProxyProvider>();
		_systemFactory.addSystemServiceProvider<ArduinoboyServiceProvider>();
		_systemFactory.addSystemServiceProvider<LsdjServiceProvider>();
		_systemFactory.addSystemServiceProvider<MgbServiceProvider>();
	}
	~RetroPlugApplication() = default;

	fw::ViewPtr onCreateUi() override {
		return std::make_shared<RetroPlugView>(_typeRegistry, _systemFactory, _ioMessageBus);
	}

	fw::AudioProcessorPtr onCreateAudio() override {
		return std::make_shared<RetroPlugProcessor>(_typeRegistry, _systemFactory, _ioMessageBus);
	}

	/*void onInitialize(fw::app::UiContext& view, fw::audio::AudioManagerPtr audio) override {
		_project.getModelFactory().addModelFactory<LsdjModel>([](std::string_view romName) {
			std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
			return shortName == "lsdj";
		});

		SystemOverlayManager* overlayManager = view.getMainWindow()->getViewManager()->createState<SystemOverlayManager>();
		overlayManager->addOverlayFactory<LsdjOverlay>([](std::string_view romName) {
			std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
			return shortName == "lsdj";
		});
	}*/
};
