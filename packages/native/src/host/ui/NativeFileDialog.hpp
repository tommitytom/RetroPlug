// A single in-flight OS-native file dialog backed by portable-file-dialogs, shared by every host that
// wants to answer the __rp_openFileBrowser seam with a real OS picker (the SDL standalone and the DPF
// plugin/standalone alike, so both behave identically and neither depends on a framework dialog).
//
// SDL2 has no file-dialog API, and DPF's own browser proved unreliable when hosted (a DAW-loaded plugin
// could bind the hook yet never surface a dialog). pfd sidesteps both: it fork/execs zenity/kdialog on
// Linux, drives IFileDialog on Windows, and Cocoa/AppleScript on macOS. Header-only — the implementation
// is inline, pulled into whichever single TU includes this.
//
// Async by design: request() launches the helper and returns immediately; the host pumps poll() from its
// UI loop (the SDL frame loop / the plugin's uiIdle) and, when the helper finishes, hands the pick to a
// callback that resolves the JS-side Promise (__rp_onFileBrowserResult). One dialog at a time — the TS
// FileSelection flow awaits sequentially.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "portable-file-dialogs.h"

namespace retroplug::ui {

class NativeFileDialog {
public:
    // True where a desktop dialog helper exists (zenity/kdialog/… on Linux; always on Windows/macOS).
    // Hosts gate the __rp_openFileBrowser bind on this so a helper-less host (a muOS handheld, a bare
    // container) leaves the hook unbound and the UI's "OS Native" toggle transparently stays in-app.
    static bool available() { return pfd::settings::available(); }

    bool active() const { return open_ || save_ || folder_; }

    // Open a picker. `patterns` is a whitespace-separated glob list (e.g. "*.gb *.gbc"); `directory` picks
    // a FOLDER (the render Output Dir); `saving` shows a save box seeded with `defaultName` under `startDir`.
    // No-op if a dialog is already in flight.
    void request(const std::string& title, const std::string& patterns, bool saving,
                 const std::string& defaultName, const std::string& startDir, bool directory) {
        if (active()) return;
        const std::string caption = title.empty() ? "Open" : title;
        // pfd filters are {label, patterns} pairs. Offer the caller's globs first, then an all-files fallback.
        std::vector<std::string> filters;
        if (!patterns.empty()) { filters.push_back("Files"); filters.push_back(patterns); }
        filters.push_back("All Files");
        filters.push_back("*");

        if (directory) {
            folder_ = std::make_unique<pfd::select_folder>(caption, startDir);
        } else if (saving) {
            // pfd takes a full default_path; join the suggested name under startDir when both are given.
            std::string defPath = defaultName;
            if (!startDir.empty() && !defaultName.empty())
                defPath = startDir + (startDir.back() == '/' ? "" : "/") + defaultName;
            else if (!startDir.empty())
                defPath = startDir;
            save_ = std::make_unique<pfd::save_file>(caption, defPath, filters);
        } else {
            open_ = std::make_unique<pfd::open_file>(caption, startDir, filters);
        }
    }

    // If the in-flight dialog has finished, hand the pick (empty string = cancel) to `deliver` and clear it.
    // Non-blocking (ready(0)); a cheap no-op when nothing is open. Call once per host UI frame.
    template <class Deliver>
    void poll(Deliver&& deliver) {
        if (open_ && open_->ready(0)) {
            auto sel = open_->result();
            open_.reset();
            deliver(sel.empty() ? std::string() : sel.front());
        } else if (save_ && save_->ready(0)) {
            std::string sel = save_->result();
            save_.reset();
            deliver(sel);
        } else if (folder_ && folder_->ready(0)) {
            std::string sel = folder_->result();
            folder_.reset();
            deliver(sel);
        }
    }

private:
    // unique_ptr sidesteps any move/copy constraints on the pfd types; whichever is set is the active dialog.
    std::unique_ptr<pfd::open_file>     open_;
    std::unique_ptr<pfd::save_file>     save_;
    std::unique_ptr<pfd::select_folder> folder_;
};

} // namespace retroplug::ui
