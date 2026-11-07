// A package's native source that includes another package's header by a climbing relative path: the TS check's
// include rule refuses it, as the configure check refuses a link to a target it does not declare.
#include "../../../other/native/src/y.hpp"

int theta() { return rp_other_y(); }
