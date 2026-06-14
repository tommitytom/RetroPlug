#pragma once

#include "rfl/TaggedUnion.hpp"

#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

// Tagged union of per-system configs. The on-disk discriminator field is
// `"kind"`; each alternative declares its own `Tag` literal — "sameboy",
// "nes", "gba" (see SameBoyConfig::Tag, MesenNesConfig::Tag,
// MesenGbaConfig::Tag).
using SystemConfig = rfl::TaggedUnion<"kind", SameBoyConfig, MesenNesConfig, MesenGbaConfig>;
