#pragma once

#include <string>
#include <utility>

namespace retroplug {

// Remembers the last successfully serialized project chunk, so a transient serializer failure cannot
// present itself to the host as "the project is empty".
//
// The problem it solves: DPF's getState returns a string and has no way to say "ask me again later".
// Whatever comes back is assigned straight into the host's state map with no validation - on the VST3
// and CLAP save paths, and also on the VST3 editor-connect path, which runs a full-state refresh. So
// an empty string from a throwing __rp_saveProjectB64 is written into the user's session as their
// project, and the next load restores nothing. Only a stderr line would say why.
//
// Returning the previous good chunk instead is strictly better here: it is a value the host already
// accepted, it is fed back verbatim to setState on load, and being one save stale beats being empty.
//
// Why "" is unambiguously a failure and never a legitimately empty project: exportBytes always puts
// project.json in the archive, so a project with zero systems still serializes to a real chunk. An
// empty string therefore means the zip failed, the JS threw, or the context is dead - never "the user
// cleared their project". Without that property this cache would resurrect a deleted project.
class LastGoodState {
public:
    // Offer the freshly serialized chunk. Returns what the host should be told: `fresh` when it holds
    // something, otherwise the last chunk that did. Empty until the first success.
    const std::string& offer(std::string fresh) {
        if (!fresh.empty()) {
            last_ = std::move(fresh);
            fallbacks_ = 0;
        } else if (!last_.empty()) {
            ++fallbacks_;
        }
        return last_;
    }

    // How many times in a row the fallback has been served. 1 on the first substitution, which is the
    // moment worth logging: past that the caller is in a failure loop and should not repeat itself.
    unsigned fallbacks() const { return fallbacks_; }

    bool empty() const { return last_.empty(); }

private:
    std::string last_;
    unsigned    fallbacks_ = 0;
};

} // namespace retroplug
