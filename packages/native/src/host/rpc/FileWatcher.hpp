#pragma once

#include <string>
#include <vector>

// The file watcher a host hands HostRpcService (spec/02): the two calls the service forwards and nothing else, so the
// service, and every includer of its header, is clear of a watcher library's headers and of its link. The native one
// (retroplug-file-watcher's NativeFileWatcher, over efsw) is what the plugin builds; a host that watches nothing passes
// nothing, and drainChangedPaths then answers empty while setWatchedRoms answers false, as watching off always did.
//
// Policy is the caller's: this reports paths that changed, and the control plane's TS decides what a change means.
class FileWatcher {
public:
    virtual ~FileWatcher() = default;

    // The paths that changed since the last call, deduplicated downstream. Called on the host's UI thread.
    virtual std::vector<std::string> drainChangedPaths() = 0;

    // Replace the set of ROM files to watch, by path: a watcher watches their parent directories and filters by
    // basename, so a ROM whose sibling changes is not reported. Called on the host's UI thread, as drainChangedPaths
    // is.
    virtual void setWatchedRoms(const std::vector<std::string>& paths) = 0;
};
