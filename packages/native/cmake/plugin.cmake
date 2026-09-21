# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# The DPF plugin: every format, the editor, and the per-platform packaging.

# --- retroplug.{vst3,clap} + the jack standalone: the DPF plugin hosting the
# Engine + control plane, with the React UI on the shared LVGL editor widget. Distinct identity from the
# legacy `retroplug` (they coexist).
dpf_add_plugin(retroplug
    TARGETS clap vst3 vst2 au jack  # au is macOS-only (DPF gates it to APPLE; a no-op elsewhere)
    USE_FILE_BROWSER                    # DPF's own (helper-free) file dialog is the fallback when no pfd helper
                                        # exists; also auto-enables DGL_USE_FILE_DROP for drag-and-drop (uiFileDropped)
    FILES_DSP
        plugin/PluginDSP.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8Link.cpp            # host serial thread -> physical N8 (streaming)
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/Edio.cpp             # N8 Pro serial client
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/WjwwoodSerialPort.cpp # serial-port factory + listSerialPorts
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8Host.cpp           # shared N8 link + config + n8.cfg
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8Hooks.cpp          # binds the __rp_*N8* config + SD-op hooks
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8Menu.cpp           # on-device menu (*t/*n/*s) for SD ops
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/n8/N8SdWorker.cpp       # background worker: ROM upload / SRAM dump+restore
        ${CP_BUNDLE_C}                # the embedded control-plane bytecode (rp_cp_bundle)
    FILES_UI
        plugin/PluginUI.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/input/GamepadManager.cpp  # SDL gamepad poll (shared UI-thread input)
        ${UI_BUNDLE_C}                # the embedded React UI bytecode (rp_ui_bundle)
        ${DPFJS_PATH}/deps/dpf-widgets/generic/LVGL.cpp)  # the generic DPF↔LVGL base widget

# plugin/ FIRST so DPF resolves OUR DistrhoPluginInfo.h ahead of DPF's own default. The `src` root
# (for "host/input/GamepadManager.hpp") arrives transitively PUBLIC via retroplug-backend.
# dpf-widgets/generic resolves "LVGL.hpp".
target_include_directories(retroplug BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
target_include_directories(retroplug PUBLIC ${DPFJS_PATH}/deps/dpf-widgets/generic)
# portable-file-dialogs (header-only): the OS-native picker backing __rp_openFileBrowser (via NativeFileDialog),
# shared with retroplug-sdl. No link libs on Linux/macOS; Windows pulls comdlg32/ole32 through its header.
target_include_directories(retroplug PUBLIC ${CMAKE_SOURCE_DIR}/deps/portable-file-dialogs)

target_link_libraries(retroplug PUBLIC
    retroplug-backend  # Engine/Backend + txiki host + (transitively) the cores + rpcpp
    lvgl-js-native                # LVGL + LvglJsEngine + lv_binding_js + lodepng (the editor)
    dpfjs::core                   # DPF Plugin + UI base
    serial)                       # wjwwood/serial -> physical Everdrive N8 Pro (Settings > N8, all formats)

target_compile_definitions(retroplug PUBLIC DPFJS_ENV_PREFIX="RETROPLUG_")

# The embedded bundles must exist before the plugin compiles them.
add_dependencies(retroplug retroplug-cp-bundle retroplug-ui-bundle)

# Windows only: the standalone (retroplug-jack, OUTPUT_NAME "retroplug") exports
# symbols, so link.exe emits an import library "retroplug.lib" — the same name,
# in the same dir, as DPF's base plugin library "retroplug.lib" that the exe also
# links, tripping LNK1149 ("output filename matches input filename"). Executables
# never emit import libraries on Linux/macOS, so this is Windows-specific. Give
# the standalone's implib a distinct name; nothing links against it.
if(WIN32 AND TARGET retroplug-jack)
    # Product-cased exe (RetroPlug.exe) to match RetroPlug.app + the branded plugin
    # bundles. Keep the implib named retroplug-jack: with OUTPUT_NAME "RetroPlug"
    # link.exe would emit RetroPlug.lib, which on the case-insensitive Windows FS
    # still collides with DPF's base "retroplug.lib" (LNK1149) — nothing links this
    # implib anyway.
    set_target_properties(retroplug-jack PROPERTIES
        OUTPUT_NAME "RetroPlug"
        ARCHIVE_OUTPUT_NAME retroplug-jack)

    # No console window when the standalone is launched directly. DPF's main() already attaches to a
    # parent console when there is one (AttachConsole(ATTACH_PARENT_PROCESS) in DistrhoPluginJACK.cpp),
    # but the exe links as a CONSOLE-subsystem app, so Windows always spawns a console. Build it as a
    # GUI-subsystem app instead: double-clicking shows no console, while running it from a terminal still
    # prints there (DPF's attach picks up the launching shell) — so debugging via `retroplug.exe` in a
    # console still works. main() (not WinMain) stays the entry via /ENTRY:mainCRTStartup.
    if(MSVC)
        target_link_options(retroplug-jack PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
elseif(APPLE AND TARGET retroplug-jack)
    # No terminal window when the standalone is launched from Finder. DPF builds the JACK standalone as a
    # bare Mach-O executable (bin/retroplug); double-clicking a bare executable in Finder opens it inside
    # Terminal.app — the macOS analogue of the Windows console popup. Package it as a .app bundle instead:
    # Finder / `open` launches it as a proper GUI app with no terminal, while running the binary inside the
    # bundle (bin/RetroPlug.app/Contents/MacOS/RetroPlug) from a shell still prints stdout/stderr there — so
    # console debugging still works. Same trade-off as the Windows /SUBSYSTEM:WINDOWS build above. The UI is
    # compiled into the binary (rp_ui_bundle), so the bundle needs no external Resources.
    #
    # OUTPUT_NAME is the bundle's on-disk name, which is what Finder / the Applications folder display — so
    # override DPF's lowercase "retroplug" to the product-cased "RetroPlug" (RetroPlug.app). This runs after
    # dpf_add_plugin, so it wins. CFBundleExecutable is derived from OUTPUT_NAME automatically. (The plugin
    # bundle DIRS are branded separately, post-build, further down — hosts key on the plugin's VST3 class /
    # CLAP id / AU codes, not the bundle filename, so that rename is purely cosmetic.)
    set_target_properties(retroplug-jack PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "RetroPlug"
        MACOSX_BUNDLE_BUNDLE_NAME "RetroPlug"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.tommitytom.retroplug")  # matches DISTRHO_PLUGIN_CLAP_ID + the AU bundle
endif()

# Windows: the single-file plugin outputs (CLAP + VST2 are plain modules there,
# not bundle dirs) take the branded name straight from OUTPUT_NAME → RetroPlug.clap
# / RetroPlug.dll. VST3 is a bundle DIR even on Windows, so it's branded via the
# post-build rename below instead. Linux keeps the lowercase Unix names.
if(WIN32)
    if(TARGET retroplug-clap)
        set_target_properties(retroplug-clap PROPERTIES OUTPUT_NAME "RetroPlug")  # RetroPlug.clap
    endif()
    if(TARGET retroplug-vst2)
        set_target_properties(retroplug-vst2 PROPERTIES OUTPUT_NAME "RetroPlug")  # RetroPlug.dll
    endif()
endif()

# macOS: give the vst3/clap/vst bundles our CFBundleIdentifier. DPF generates the AU (.component) Info.plist
# via DistrhoPluginExport, which correctly uses DISTRHO_PLUGIN_CLAP_ID (net.tommitytom.retroplug); but the
# vst3/clap/vst bundles are configure_file'd from DPF's shared template, which hardcodes the framework
# author's placeholder domain (studio.kx.distrho.<name>). Rather than fork the generic template, rewrite the
# identifier post-build so every RetroPlug bundle is consistent under our domain. Keep this value in sync
# with DISTRHO_PLUGIN_CLAP_ID (plugin/DistrhoPluginInfo.h) — the single source of truth for the AU + CLAP id.
if(APPLE)
    set(_rp_bundle_id "net.tommitytom.retroplug")
    # target -> on-disk bundle dir (the vst2 target's bundle extension is .vst, not .vst2)
    foreach(_pair "retroplug-vst3:retroplug.vst3" "retroplug-clap:retroplug.clap" "retroplug-vst2:retroplug.vst")
        string(REPLACE ":" ";" _p "${_pair}")
        list(GET _p 0 _tgt)
        list(GET _p 1 _bundle)
        if(TARGET ${_tgt})
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND /usr/bin/plutil -replace CFBundleIdentifier -string "${_rp_bundle_id}"
                        "${CMAKE_BINARY_DIR}/bin/${_bundle}/Contents/Info.plist"
                COMMENT "Setting CFBundleIdentifier=${_rp_bundle_id} on ${_bundle}"
                VERBATIM)
        endif()
    endforeach()
endif()

# macOS distribution: embed SDL2.framework into every SDL2-linked bundle.
# ---------------------------------------------------------------------------
# The framework build (RETROPLUG_SDL2_FRAMEWORK) links SDL2 by its install name
# @rpath/SDL2.framework/Versions/A/SDL2 (GamepadManager + backend pull SDL2 into
# the standalone AND every plugin format). Nothing on an end-user machine
# provides that framework, so copy the (universal) framework into each bundle's
# Contents/Frameworks and add an @loader_path/../Frameworks rpath to resolve it.
# The embedded framework is re-signed with our Developer ID by the CI codesign
# step (.github/workflows/release.yml). retroplug-cli links no SDL2, so it is
# excluded. The dev build (pkg-config / Homebrew SDL2) needs none of this — the
# machine already has SDL2 — so this is gated on RETROPLUG_SDL2_FRAMEWORK.
if(APPLE AND RETROPLUG_SDL2_FRAMEWORK)
    # ${SDL2_FRAMEWORK} is the .framework directory (find_library return, root
    # CMakeLists). target -> on-disk bundle dir (vst2's extension is .vst).
    foreach(_pair "retroplug-jack:RetroPlug.app" "retroplug-vst3:retroplug.vst3"
                  "retroplug-clap:retroplug.clap" "retroplug-vst2:retroplug.vst"
                  "retroplug-au:retroplug.component")
        string(REPLACE ":" ";" _p "${_pair}")
        list(GET _p 0 _tgt)
        list(GET _p 1 _bundle)
        if(TARGET ${_tgt})
            # Resolve @rpath/SDL2.framework/... against the embedded copy. dyld
            # tries each rpath in turn, so the build-machine's absolute framework
            # rpath (added automatically by CMake) still works locally.
            set_property(TARGET ${_tgt} APPEND PROPERTY
                BUILD_RPATH "@loader_path/../Frameworks")
            # cp -R preserves the framework's Versions/Current symlink structure
            # (CMake's copy_directory would flatten it and break codesign).
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -rf
                        "${CMAKE_BINARY_DIR}/bin/${_bundle}/Contents/Frameworks/SDL2.framework"
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${CMAKE_BINARY_DIR}/bin/${_bundle}/Contents/Frameworks"
                COMMAND /bin/cp -R "${SDL2_FRAMEWORK}"
                        "${CMAKE_BINARY_DIR}/bin/${_bundle}/Contents/Frameworks/"
                COMMENT "Embedding SDL2.framework into ${_bundle}"
                VERBATIM)
        endif()
    endforeach()
endif()

# Brand the on-disk plugin bundle NAMES to the product casing on the GUI platforms
# (macOS + Windows — both case-insensitive filesystems). DPF hardcodes the lowercase
# plugin name into each bundle's DIRECTORY path (bin/retroplug.vst3/…), which
# OUTPUT_NAME can't override, so rename the outer directory post-build to RetroPlug.*.
# Only the directory name changes — the inner binary + Info.plist (CFBundleExecutable)
# stay "retroplug", so the code-signature / notarization seal (which covers Contents/,
# not the directory name) and host loading (via CFBundleExecutable) are unaffected.
# `cmake -E rename` is an idempotent no-op on later builds (source and dest are the
# same inode on a case-insensitive FS). Linux stays lowercase (Unix convention +
# case-sensitive FS); its single-file CLAP/VST2 — and Windows' — are branded via
# OUTPUT_NAME above, so only the bundle DIRS need renaming here.
if(APPLE OR WIN32)
    set(_rp_renames "")
    if(APPLE)
        list(APPEND _rp_renames
            "retroplug-vst3:retroplug.vst3:RetroPlug.vst3"
            "retroplug-clap:retroplug.clap:RetroPlug.clap"
            "retroplug-au:retroplug.component:RetroPlug.component"
            "retroplug-vst2:retroplug.vst:RetroPlug.vst")
    elseif(WIN32)
        list(APPEND _rp_renames "retroplug-vst3:retroplug.vst3:RetroPlug.vst3")
    endif()
    foreach(_triple ${_rp_renames})
        string(REPLACE ":" ";" _t "${_triple}")
        list(GET _t 0 _tgt)
        list(GET _t 1 _from)
        list(GET _t 2 _to)
        if(TARGET ${_tgt})
            # Registered after the CFBundleIdentifier + SDL2-embed POST_BUILD steps
            # above, so those run against the lowercase dir first, then this renames.
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rename
                        "${CMAKE_BINARY_DIR}/bin/${_from}"
                        "${CMAKE_BINARY_DIR}/bin/${_to}"
                COMMENT "Branding bundle name: ${_from} -> ${_to}"
                VERBATIM)
        endif()
    endforeach()
endif()
