# After alpha's directory, so a link from alpha to one of these is to a target defined later: what the deferred check
# exists to see.
add_library(retroplug-beta INTERFACE)
add_library(retroplug-beta-helper INTERFACE)
# The namespaced name a library is usually exported under: the same target, judged through the alias.
add_library(rp::beta ALIAS retroplug-beta)
