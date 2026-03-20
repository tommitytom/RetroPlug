#pragma once

#include <queue>
#include <mutex>
#include <cstdint>

#include "Core/SystemTypes.h"
#include "Core/NES/INesMemoryHandler.h"

namespace rp {
	class NesEverdriveFifo : public INesMemoryHandler {
	private:
		std::queue<uint8_t> _rxQueue;
		std::mutex _mutex;

	public:
		// --- Called from audio/emulation thread; no lock needed when single-threaded ---
		void GetMemoryRanges(MemoryRanges& ranges) override {
			// Register as handler for both data and status registers
			ranges.SetAllowOverride();
			ranges.AddHandler(MemoryOperation::Read, 0x40F0, 0x40F1);
			ranges.AddHandler(MemoryOperation::Write, 0x40F0, 0x40F1);
		}

		uint8_t ReadRam(uint16_t addr) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (addr == 0x40F1) {
				// bit7=1 → empty, bit7=0 → data present
				return _rxQueue.empty() ? 0x80 : 0x00;
			}
			if (addr == 0x40F0) {
				if (!_rxQueue.empty()) {
					uint8_t val = _rxQueue.front();
					_rxQueue.pop();
					return val;
				}
				return 0xFF; // shouldn't be called without checking status first
			}
			return 0xFF;
		}

		uint8_t PeekRam(uint16_t addr) override {
			// PeekRam is used by the debugger and must not have side effects
			std::lock_guard<std::mutex> lock(_mutex);
			if (addr == 0x40F1) return _rxQueue.empty() ? 0x80 : 0x00;
			if (addr == 0x40F0) return _rxQueue.empty() ? 0xFF : _rxQueue.front();
			return 0xFF;
		}

		void WriteRam(uint16_t addr, uint8_t value) override {
			// NES→host writes (commands, MIDI TX) — ignore for now.
			// If you later need to emulate ed_init()'s CMD_FPG_CFG handshake,
			// parse the command protocol here.
			(void)addr; (void)value;
		}

		// --- Called from MIDI callback or audio thread ---
		void pushByte(uint8_t byte) {
			std::lock_guard<std::mutex> lock(_mutex);
			_rxQueue.push(byte);
		}
	};
}
