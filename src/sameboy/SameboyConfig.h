#pragma once

namespace rp {
	enum class GameboyModel {
		Auto,
		DmgB,
		//SgbNtsc,
		//SgbPal,
		//Sgb2,
		CgbC,
		CgbE,
		Agb
	};

	struct SameboyConfig {
		GameboyModel model = GameboyModel::Auto;
		bool fastBoot = true;
	};
}