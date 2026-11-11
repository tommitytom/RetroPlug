# beta-lib is a library beta defines and does not list in its manifest (an unlisted target, as retroplug-midi and
# retroplug-audio are), so the only way this link can count as a use is the checker reading beta's CMakeLists.
# gamma-lib is defined by gamma the same way and linked by nothing here.
add_library(alpha-native STATIC native.cpp)
target_link_libraries(alpha-native PRIVATE beta-lib)
