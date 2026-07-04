#pragma once

#include <cctype>
#include <string>
#include <string_view>

// Single source of truth for the on-disk *schema* versions RetroPlug stamps on
// save and validates on load. This is detection groundwork, NOT a migration
// transform pipeline: reads are forward-tolerant (rfl::DefaultIfMissing), so
// additive/removed field changes don't break old files and do NOT bump anything.
//
// Bump a constant ONLY on a *breaking* (non-additive) change — a field rename, a
// restructure, or a changed semantic that an old file would misinterpret. When
// you do, handle the resulting `Older` files at the matching load seam (that's
// where a future migration transform hooks in). A file stamped NEWER than the
// running build is refused (a format from the future can't be safely read).
//
// See AGENTS.md ("no versioned migrations, but reads are forward-tolerant").

namespace rp::schema {

// Current versions, one per serialized root. Baselines match today's struct
// defaults, so introducing this check is a no-op for existing files.
constexpr int kProject    = 1;   // ProjectConfig  (.rplg / DAW state chunk)
constexpr int kUserConfig = 1;   // config.json    (UserConfigJson)
constexpr int kBindings   = 1;   // bindings/*.json (BindingMapJson)
constexpr int kRecent     = 2;   // recent.json    (RecentFilesJson)

enum class Check { Ok, Older, Newer };

// Compare a file's stamped version to the current build's.
//   Ok    — same version, load normally.
//   Older — predates the current build; load (forward-tolerant) and, once a
//           breaking change exists, migrate at the load seam.
//   Newer — saved by a future build; refuse (unknown/incompatible semantics).
inline Check checkVersion(int fileVersion, int current) {
    if (fileVersion == current) return Check::Ok;
    return fileVersion < current ? Check::Older : Check::Newer;
}

// ProjectConfig.schemaVersion is a legacy *string* ("1.0") — kept a string so
// old `.rplg` files (which carry `"1.0"`) still parse; a present-but-mistyped
// field is not rescued by DefaultIfMissing. Parse its leading integer for the
// comparison: "1.0" -> 1, "2" -> 2. An empty/garbage value floors to kProject
// (treat as the current baseline rather than spuriously "older").
inline int parseProjectVersion(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) != 0)) ++i;
    std::size_t start = i;
    int value = 0;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) != 0)) {
        value = value * 10 + (s[i] - '0');
        ++i;
    }
    return i > start ? value : kProject; // no leading digits -> baseline floor
}

} // namespace rp::schema
