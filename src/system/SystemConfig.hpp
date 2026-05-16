#pragma once

#include "rfl/TaggedUnion.hpp"

#include "system/mesen/GbaConfig.hpp"
#include "system/mesen/MesenConfig.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

// Tagged union of per-system configs. The on-disk discriminator field is
// `"kind"`; each alternative declares its own `Tag` literal (see
// SameBoyConfig::Tag, MesenConfig::Tag, GbaConfig::Tag). The spelling is
// part of the public schema once step 12 lands — do not rename without a
// schema bump.
using SystemConfig = rfl::TaggedUnion<"kind", SameBoyConfig, MesenConfig, GbaSystemConfig>;
