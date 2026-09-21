# Included by packages/native/CMakeLists.txt, which is the only caller. `include()` keeps the
# CURRENT directory scope, so ${CMAKE_CURRENT_SOURCE_DIR} still means packages/native/ here and every
# relative source path below resolves exactly as it did inline. (add_subdirectory would NOT: it opens
# a child scope and re-roots that variable, which is the trap this split is avoiding.)
#
# The Node-API addon host. Opt-in at configure; not part of a default build.

# --- retroplug.node: the Node-API host ---------------------------------------------------------------
# The fourth host over the same backend service graph (plugin / CLI / render worker / Node), swapping the
# QuickJS codec for an N-API one (node/NodeCodec.hpp) so packages/retroplug/src — which has no txiki
# coupling — runs unmodified on Node. __rpcSend stays SYNCHRONOUS, which an in-process addon can honour
# and an out-of-process stdio client cannot.
#
# OFF by default: it needs Node's N-API headers, which a build box need not have. Turn it on through the
# usual entry point, which passes -D straight to the configure:
#     ./build.sh -DRETROPLUG_NODE_ADDON=ON
# Linux/macOS for now (Windows wants an import lib from the node distribution).
option(RETROPLUG_NODE_ADDON "Build the Node-API addon host (retroplug.node)" OFF)
if(RETROPLUG_NODE_ADDON)
    # nvm / homebrew / any non-system node keeps its headers next to the binary, not in /usr/include,
    # so ask the node on PATH where it lives before falling back to the system spellings.
    find_program(NODE_EXECUTABLE NAMES node nodejs)
    if(NODE_EXECUTABLE)
        get_filename_component(_node_bin "${NODE_EXECUTABLE}" REALPATH)
        get_filename_component(_node_prefix "${_node_bin}" DIRECTORY)
        get_filename_component(_node_prefix "${_node_prefix}" DIRECTORY)
    endif()
    find_path(NODE_API_INCLUDE_DIR node_api.h
        HINTS ${NODE_INCLUDE_DIR} ${_node_prefix}/include/node
              /usr/include/node /usr/local/include/node
    )
    if(NOT NODE_API_INCLUDE_DIR)
        message(FATAL_ERROR
            "RETROPLUG_NODE_ADDON=ON but node_api.h was not found. "
            "Install the Node headers or pass -DNODE_INCLUDE_DIR=<dir containing node_api.h>.")
    endif()

    add_library(retroplug-node MODULE node/binding.cpp)
    target_include_directories(retroplug-node PRIVATE
        ${NODE_API_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/node
    )
    target_link_libraries(retroplug-node PRIVATE retroplug-backend)
    set_target_properties(retroplug-node PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        PREFIX ""                 # `retroplug.node`, not `libretroplug.node`
        OUTPUT_NAME "retroplug"
        SUFFIX ".node"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/node"
    )
    if(APPLE)
        # The napi_* symbols are resolved by the loading node process, not linked here.
        target_link_options(retroplug-node PRIVATE -undefined dynamic_lookup)
    endif()
endif()
