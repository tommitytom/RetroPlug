#pragma once

#include <string>

#include <rfl/DefaultIfMissing.hpp>
#include <rfl/Result.hpp>
#include <rfl/json.hpp>

#include "lsdj/model/Sav.hpp"
#include "lsdj/model/Song.hpp"

// JSON (de)serialization for the C++ LSDj sav/song model (rfl::json idiom). The
// shipping codec is now pure TS (packages/retroplug/src/lsdj), which is the single
// source of truth. This reflect-cpp model + codec are retained ONLY as the test
// oracle: retroplug-lsdj-diff-tests cross-checks decode against liblsdj, and
// retroplug-lsdj-golden-dump freezes savToJson(decodeSav(..)) as the golden vectors
// the pure-TS codec is validated against. Not part of any shipping target.
namespace rp::lsdj {

inline std::string savToJson(const model::Sav& sav) { return rfl::json::write(sav); }

inline rfl::Result<model::Sav> savFromJson(const std::string& json) {
    return rfl::json::read<model::Sav>(json);
}

inline std::string songToJson(const model::Song& song) { return rfl::json::write(song); }

inline rfl::Result<model::Song> songFromJson(const std::string& json) {
    return rfl::json::read<model::Song>(json);
}

// Lenient variants for test fixtures: missing fields take their model defaults,
// so a fixture can specify only the fields it cares about
// (e.g. {"workingSong":{"settings":{"tempo":150}}}).
inline rfl::Result<model::Sav> savFromJsonFixture(const std::string& json) {
    return rfl::json::read<model::Sav, rfl::DefaultIfMissing>(json);
}

inline rfl::Result<model::Song> songFromJsonFixture(const std::string& json) {
    return rfl::json::read<model::Song, rfl::DefaultIfMissing>(json);
}

} // namespace rp::lsdj
