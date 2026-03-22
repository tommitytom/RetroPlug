#pragma once

class Emulator;

namespace rp {
	class MesenAudioDevice;
	class MesenVideoDevice;
	class NesEverdriveFifo;
	class EdioProxy;

	static constexpr double	CPU_CLOCK_RATE = 1789773.0;
	static constexpr int	PPU_DIVIDER = 3;		// PPU runs at 3x CPU clock (NTSC)

	// The FIFO registers the ROM polls.
	static constexpr uint16_t	FIFO_STATUS_ADDR = 0x4150;
	static constexpr uint16_t	FIFO_DATA_ADDR = 0x4151;

	enum class MesenSystemType {
		Nes,
		Snes,
		Gameboy,
		PcEngine,
		Sms,
		Cv,
		Gba,
		Ws,
		None
	};

	struct MesenComponent {
		MesenSystemType type = MesenSystemType::None;
	};

	struct MesenStateComponent {
		std::shared_ptr<MesenAudioDevice>  audioDevice;
		std::shared_ptr<MesenVideoDevice>  videoDevice;
		std::shared_ptr<NesEverdriveFifo>  fifo;
		std::unique_ptr<Emulator>          emulator;

		std::shared_ptr<EdioProxy> edioProxy;
	};
}
