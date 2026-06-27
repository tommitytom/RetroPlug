#pragma once

// Single source of truth for the PluginRpcService method surface.
// Used by both PluginJsBridge (runtime dispatcher) and RpcSchemaDump
// (schema generator for the typed TS client). Adding a method here exposes
// it to JS and to the generated client in one step.

#include "PluginRpcService.hpp"

template <class Server>
void registerPluginRpcMethods(Server& server) {
    server.template addMethod<&PluginRpcService::getFrame>();
    server.template addMethod<&PluginRpcService::openRomBrowser>();
    server.template addMethod<&PluginRpcService::openSaveProjectBrowser>();
    server.template addMethod<&PluginRpcService::openExportZipBrowser>();
    server.template addMethod<&PluginRpcService::openLoadProjectBrowser>();
    server.template addMethod<&PluginRpcService::getUnsavedSummary>();
    server.template addMethod<&PluginRpcService::getCurrentProjectPath>();
    server.template addMethod<&PluginRpcService::saveDirtySram>();
    server.template addMethod<&PluginRpcService::saveProject>();
    server.template addMethod<&PluginRpcService::quitStandalone>();
    server.template addMethod<&PluginRpcService::loadProjectFromPath>();
    server.template addMethod<&PluginRpcService::getMissingFiles>();
    server.template addMethod<&PluginRpcService::relinkMissingFile>();
    server.template addMethod<&PluginRpcService::openRelinkBrowser>();
    server.template addMethod<&PluginRpcService::cancelMissingFiles>();
    server.template addMethod<&PluginRpcService::loadRomFromPath>();
    server.template addMethod<&PluginRpcService::addRomFromPath>();
    server.template addMethod<&PluginRpcService::replaceRomFromPath>();
    server.template addMethod<&PluginRpcService::removeSystem>();
    server.template addMethod<&PluginRpcService::duplicateSystem>();
    server.template addMethod<&PluginRpcService::clearCurrentProjectPath>();
    server.template addMethod<&PluginRpcService::listSystems>();
    server.template addMethod<&PluginRpcService::setFocus>();
    server.template addMethod<&PluginRpcService::getFocus>();
    server.template addMethod<&PluginRpcService::pressButton>();
    server.template addMethod<&PluginRpcService::setLinkGroupId>();
    server.template addMethod<&PluginRpcService::getMidiRouting>();
    server.template addMethod<&PluginRpcService::setMidiRouting>();
    server.template addMethod<&PluginRpcService::getAudioRouting>();
    server.template addMethod<&PluginRpcService::setAudioRouting>();
    server.template addMethod<&PluginRpcService::getZoom>();
    server.template addMethod<&PluginRpcService::getProjectZoom>();
    server.template addMethod<&PluginRpcService::setZoom>();
    server.template addMethod<&PluginRpcService::getLayout>();
    server.template addMethod<&PluginRpcService::setLayout>();
    server.template addMethod<&PluginRpcService::resetSystem>();
    server.template addMethod<&PluginRpcService::newSram>();
    server.template addMethod<&PluginRpcService::setFastBoot>();
    server.template addMethod<&PluginRpcService::setModel>();
    server.template addMethod<&PluginRpcService::setHighpass>();
    server.template addMethod<&PluginRpcService::setReloadOnRomChange>();
    server.template addMethod<&PluginRpcService::setLsdjSyncConfig>();
    server.template addMethod<&PluginRpcService::setWindowSize>();
    server.template addMethod<&PluginRpcService::isWindowSizeControlled>();
    server.template addMethod<&PluginRpcService::getKitsConfig>();
    server.template addMethod<&PluginRpcService::compileAndPatchKit>();
    server.template addMethod<&PluginRpcService::auditionSample>();
    server.template addMethod<&PluginRpcService::eraseKit>();
    server.template addMethod<&PluginRpcService::openSampleBrowser>();
    server.template addMethod<&PluginRpcService::getUserConfig>();
    server.template addMethod<&PluginRpcService::setActiveKeyboardBindings>();
    server.template addMethod<&PluginRpcService::setActiveGamepadBindings>();
    server.template addMethod<&PluginRpcService::setAutoSaveSram>();
    server.template addMethod<&PluginRpcService::setDefaultZoom>();
    server.template addMethod<&PluginRpcService::getBindingProfile>();
    server.template addMethod<&PluginRpcService::saveBindingProfile>();
    server.template addMethod<&PluginRpcService::renameBindingProfile>();
    server.template addMethod<&PluginRpcService::deleteBindingProfile>();
    server.template addMethod<&PluginRpcService::openSettingsFolder>();
    server.template addMethod<&PluginRpcService::saveSram>();
    server.template addMethod<&PluginRpcService::openSaveSramBrowser>();
    server.template addMethod<&PluginRpcService::openLoadSramBrowser>();
    server.template addMethod<&PluginRpcService::saveState>();
    server.template addMethod<&PluginRpcService::openSaveStateBrowser>();
    server.template addMethod<&PluginRpcService::openLoadStateBrowser>();
    server.template addMethod<&PluginRpcService::getRecentFiles>();
    server.template addMethod<&PluginRpcService::removeRecentFile>();
    server.template addMethod<&PluginRpcService::renameRecentFile>();
    server.template addMethod<&PluginRpcService::openRecentRelinkBrowser>();
    server.template addMethod<&PluginRpcService::getMemory>();
    server.template addMethod<&PluginRpcService::subscribeMemory>();
    server.template addMethod<&PluginRpcService::unsubscribeMemory>();
    server.addDiscoveryMethod();
}
