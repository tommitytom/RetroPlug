#pragma once

#include "rfl/TaggedUnion.hpp"

#include "system/sameboy/SameBoyConfig.hpp"

// Tagged union of per-system configs. The on-disk discriminator field is
// `"kind"`; each alternative declares its own `Tag` literal (see
// SameBoyConfig::Tag). The spelling is part of the public schema once
// step 12 lands — do not rename without a schema bump.
//
// MesenConfig is not present yet; add it (with its own `Tag = rfl::Literal<"mesen">`)
// when MesenSystem lands at Step 17.
using SystemConfig = rfl::TaggedUnion<"kind", SameBoyConfig>;
