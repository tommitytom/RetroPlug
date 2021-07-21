#pragma once

#include "version.h"

#define SEMVER_MAJOR 0
#define SEMVER_MINOR 3
#define SEMVER_PATCH 1

#define PLUG_NAME "RetroPlug"
#define PLUG_MFR "tommitytom"
#define PLUG_VERSION_HEX 0x00000301
#define PLUG_VERSION_STR VERSION_STRING(SEMVER_MAJOR, SEMVER_MINOR, SEMVER_PATCH)
#define PLUG_UNIQUE_ID '2wvF'
#define PLUG_MFR_ID 'tmtt'
#define PLUG_URL_STR "https://tommitytom.co.uk"
#define PLUG_EMAIL_STR "retroplug@tommitytom.co.uk"
#define PLUG_COPYRIGHT_STR "Copyright 2021 Tom Yaxley"
#define PLUG_CLASS_NAME RetroPlugInstrument

#define BUNDLE_NAME "RetroPlug"
#define BUNDLE_MFR "tommitytom"
#define BUNDLE_DOMAIN "com"

#ifdef APP_API
#define PLUG_CHANNEL_IO "0-2"
#else
#define PLUG_CHANNEL_IO "0-8"
#endif

#define PLUG_LATENCY 0
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 1
#define PLUG_DOES_MPE 1
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 320
#define PLUG_HEIGHT 288
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0

#define AUV2_ENTRY RetroPlug_Entry
#define AUV2_ENTRY_STR "RetroPlug_Entry"
#define AUV2_FACTORY RetroPlug_Factory
#define AUV2_VIEW_CLASS RetroPlug_View
#define AUV2_VIEW_CLASS_STR "RetroPlug_View"

#define AAX_TYPE_IDS 'EFN1', 'EFN2'
#define AAX_PLUG_MFR_STR "tommitytom"
#define AAX_PLUG_NAME_STR "RetroPlug\nIPIS"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Synth"

#define VST3_SUBCATEGORY "Instrument"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_RESIZABLE 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTTO_FN "fonts/Roboto-Regular.ttf"
#define GAMEBOY_FN "fonts/Early-GameBoy.ttf"
