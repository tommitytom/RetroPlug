#pragma once

#include "rfl/TaggedUnion.hpp"

#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
#include "system/sameboy/roles/MgbPassthroughRole.hpp"

// Tagged union of role configs attached to a SameBoy system. Discriminator
// field is `"kind"`; each alternative declares its own `Tag` literal.
using RoleConfig = rfl::TaggedUnion<"kind",
                                    MgbRoleConfig,
                                    LsdjSyncConfig,
                                    rp::lsdj::LsdjKitPatchConfig>;
