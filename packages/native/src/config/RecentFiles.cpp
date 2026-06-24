#include "RecentFiles.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

#include "RecentFilesSerialization.hpp"
#include "UserConfig.hpp"   // for resolveDefaultUserConfigDir()

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::string out(static_cast<std::size_t>(size), '\0');
    if (!in.read(out.data(), size)) return {};
    return out;
}

// weakly_canonical normalises ./, ../ and symlinks where possible without
// requiring the file to exist. Falls back to absolute() on any error so we
// still produce a stable string for dedup.
std::string canonicalize(const std::string& raw) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(raw), ec);
    if (ec || p.empty()) {
        p = fs::absolute(fs::path(raw), ec);
        if (ec) return raw;
    }
    return p.string();
}

} // namespace

RecentFiles::RecentFiles(fs::path rootOverride)
    : rootOverride_(std::move(rootOverride)) {}

void RecentFiles::start() {
    const fs::path rootDir = rootOverride_.empty()
        ? resolveDefaultUserConfigDir()
        : rootOverride_;
    if (rootDir.empty()) {
        std::fprintf(stderr, "[recent-files] no resolvable config dir; in-memory only\n");
        return;
    }

    std::error_code ec;
    fs::create_directories(rootDir, ec);
    if (ec) {
        std::fprintf(stderr, "[recent-files] failed to create %s: %s\n",
                     rootDir.string().c_str(), ec.message().c_str());
        return;
    }

    recentFile_ = rootDir / "recent.json";

    std::string text = slurp(recentFile_);
    if (text.empty()) return;

    auto parsed = recentFilesFromJson(text);
    if (!parsed) {
        std::fprintf(stderr,
            "[recent-files] %s parse failed — starting from empty list\n",
            recentFile_.string().c_str());
        return;
    }

    std::vector<RecentFileEntry> next;
    next.reserve(parsed->entries.size());
    for (auto& e : parsed->entries) {
        if (e.path.empty()) continue;
        next.push_back(RecentFileEntry{std::move(e.path), std::move(e.name)});
        if (next.size() >= kMaxEntries) break;
    }

    std::lock_guard<std::mutex> lock(mu_);
    entries_ = std::move(next);
}

std::vector<RecentFileEntry> RecentFiles::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return entries_;
}

bool RecentFiles::add(const std::string& path, const std::string& name) {
    if (path.empty()) return false;

    const std::string canon = canonicalize(path);

    {
        std::lock_guard<std::mutex> lock(mu_);
        // Carry an existing alias forward unless the caller supplies a new one,
        // so a rename survives re-opening the same project.
        std::string keepName = name;
        if (keepName.empty()) {
            auto it = std::find_if(entries_.begin(), entries_.end(),
                [&](const RecentFileEntry& e) { return e.path == canon; });
            if (it != entries_.end()) keepName = it->name;
        }
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [&](const RecentFileEntry& e) { return e.path == canon; }),
            entries_.end());
        entries_.insert(entries_.begin(), RecentFileEntry{canon, std::move(keepName)});
        if (entries_.size() > kMaxEntries) entries_.resize(kMaxEntries);
    }

    return writeAndNotify();
}

bool RecentFiles::remove(const std::string& path) {
    if (path.empty()) return false;
    const std::string canon = canonicalize(path);

    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto before = entries_.size();
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [&](const RecentFileEntry& e) { return e.path == canon; }),
            entries_.end());
        if (entries_.size() == before) return false;   // nothing removed
    }

    return writeAndNotify();
}

bool RecentFiles::relink(const std::string& oldPath, const std::string& newPath) {
    if (oldPath.empty() || newPath.empty()) return false;
    const std::string oldCanon = canonicalize(oldPath);
    const std::string newCanon = canonicalize(newPath);

    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto idx = std::find_if(entries_.begin(), entries_.end(),
            [&](const RecentFileEntry& e) { return e.path == oldCanon; });
        if (idx == entries_.end()) return false;
        idx->path = newCanon;
        // Repoint done; drop any *other* entry that now collides with newCanon,
        // keeping the relinked one (the first match in scan order).
        bool seen = false;
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [&](const RecentFileEntry& e) {
                    if (e.path != newCanon) return false;
                    if (!seen) { seen = true; return false; }   // keep first
                    return true;                                // drop the rest
                }),
            entries_.end());
    }

    return writeAndNotify();
}

bool RecentFiles::rename(const std::string& path, const std::string& name) {
    if (path.empty()) return false;
    const std::string canon = canonicalize(path);

    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [&](const RecentFileEntry& e) { return e.path == canon; });
        if (it == entries_.end()) return false;
        it->name = name;
    }

    return writeAndNotify();
}

bool RecentFiles::writeAndNotify() {
    std::string contents;
    {
        std::lock_guard<std::mutex> lock(mu_);
        RecentFilesJson out;
        out.entries.reserve(entries_.size());
        for (const auto& e : entries_) {
            out.entries.push_back(RecentFileJson{e.path, e.name});
        }
        contents = recentFilesToJson(out);
    }

    bool wrote = true;
    if (!recentFile_.empty()) {
        wrote = writeAtomic(contents);
        if (!wrote) {
            std::fprintf(stderr, "[recent-files] failed to write %s\n",
                         recentFile_.string().c_str());
        }
    }

    if (onChange_) onChange_();
    return wrote;
}

bool RecentFiles::writeAtomic(const std::string& contents) const {
    const fs::path tmp = recentFile_.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!out.good()) return false;
    }
    std::error_code ec;
    fs::rename(tmp, recentFile_, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}
