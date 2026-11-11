# A package's target may start with a dependency's target name and mean nothing by it: "gamma-lib-alpha" is this
# package's own library, and no "-" makes it a use of gamma's "gamma-lib". beta-lib, on the other hand, is a real link.
# The checking script must tell them apart - reading "gamma-lib-alpha" as gamma would be a use that is not there, and
# the warning for gamma would never fire.
add_library(gamma-lib-alpha STATIC native.cpp)
add_library(gamma-lib-alpha-interface INTERFACE)
target_link_libraries(gamma-lib-alpha PRIVATE beta-lib)
