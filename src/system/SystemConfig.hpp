#pragma once

#include <variant>

#include "system/sameboy/SameBoyConfig.hpp"

// Tagged union of per-system configs. reflectcpp serializes std::variant via a
// tagged-union convention; pick the alternative names as the stable on-disk
// discriminator (treat them as a public schema once Step 12 lands).
//
// MesenConfig is not present yet; add it when MesenSystem lands at Step 10.
using SystemConfig = std::variant<SameBoyConfig>;
