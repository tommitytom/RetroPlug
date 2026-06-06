// Prints the JSON Schema for the LSDj Sav model to stdout, for the zod/TS
// codegen (tools/gen-sav-ts.js). reflect-cpp's to_schema inspects compile-time
// type info only — the Sav def transitively pulls in Song, Instrument, etc.,
// so one barrel schema covers the whole model.
//
// Emits the schema directly (not via the OpenRPC/TypedRpcServer wrapper) so the
// barrel uses #/$defs/ refs the json-schema-to-zod codegen can resolve.
#include <iostream>

#include <rfl/json.hpp>

#include "lsdj/model/Sav.hpp"

int main() {
    std::cout << rfl::json::to_schema<rp::lsdj::model::Sav>() << std::endl;
    return 0;
}
