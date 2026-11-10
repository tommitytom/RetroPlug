# After alpha's directory, so a link from alpha to one of these is to a target defined later: what the deferred check
# exists to see.
add_library(retroplug-beta INTERFACE)
add_library(retroplug-beta-helper INTERFACE)
# The namespaced name a library is usually exported under: the same target, judged through the alias.
add_library(rp::beta ALIAS retroplug-beta)
if(RP_GATHER_CASE STREQUAL "objects" OR RP_GATHER_CASE STREQUAL "objects-declared")
    # The objects the $<TARGET_OBJECTS:...> cases name: what this library compiles is beta's.
    add_library(retroplug-beta-objects OBJECT native/src/x.cpp)
endif()
