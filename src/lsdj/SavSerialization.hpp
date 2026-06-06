#pragma once

#include <string>

#include <rfl/Result.hpp>
#include <rfl/json.hpp>

#include "lsdj/model/Sav.hpp"
#include "lsdj/model/Song.hpp"

// JSON (de)serialization for the LSDj sav/song model, mirroring the project's
// existing rfl::json idiom (src/project/ProjectSerialization.hpp). The model is
// the single source of truth: the same reflect-cpp structs drive JSON here and
// the generated zod/TS schema (see tools/gen-sav-ts.js).
namespace rp::lsdj {

inline std::string savToJson(const model::Sav& sav) { return rfl::json::write(sav); }

inline rfl::Result<model::Sav> savFromJson(const std::string& json) {
    return rfl::json::read<model::Sav>(json);
}

inline std::string songToJson(const model::Song& song) { return rfl::json::write(song); }

inline rfl::Result<model::Song> songFromJson(const std::string& json) {
    return rfl::json::read<model::Song>(json);
}

} // namespace rp::lsdj
