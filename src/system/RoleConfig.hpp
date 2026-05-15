#pragma once

#include "rfl/TaggedUnion.hpp"

#include "system/sameboy/roles/MgbPassthroughRole.hpp"

// Tagged union of role configs attached to a SameBoy system. Discriminator
// field is `"kind"`; each alternative declares its own `Tag` literal.
//
// LSDJ sync (step 08), Arduinoboy (step 09) and kit-patch (step 10) each add
// a new alternative here. The spelling is part of the public schema once
// step 12 lands — do not rename without a schema bump.
using RoleConfig = rfl::TaggedUnion<"kind", MgbRoleConfig>;
