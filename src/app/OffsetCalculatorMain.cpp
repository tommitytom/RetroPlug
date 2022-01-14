#include <spdlog/spdlog.h>

#include "sameboy/OffsetCalculator.h"
#include "util/fs.h"

using namespace rp;

int main() {
	lsdj::OffsetCalculator::Context ctx;

	auto romData = fsutil::readFile("C:\\retro\\LSDj-v5.0.3.gb");

	if (lsdj::OffsetCalculator::calculate((const char*)romData.data(), ctx, "LSDj-v5.0.3.gb")) {
		spdlog::info("Found offsets");
	} else {
		spdlog::error("Could not find offsets ;(");
	}

	return 0;
}
