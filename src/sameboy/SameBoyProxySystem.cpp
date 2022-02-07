#include "SameBoyProxySystem.h"

#include <iostream>

using namespace rp;

void SameBoyProxySystem::setup(SameBoySystem& system) {
	_resolution = system.getResolution();
	
	system.saveState(_state);
	system.getMemory(MemoryType::Rom).getBuffer().copyTo(&_rom);
	getSameboyStateOffsets(system.getState().gb, &_stateOffsets);
}

void SameBoyProxySystem::process(uint32 frameCount) {
	SystemIo* io = getStream().get();

	if (io && io->output.state) {
		io->output.state->copyTo(&_state);
	}
}

void SameBoyProxySystem::reset() {
	SystemIo* io = getStream().get();

	if (io) {
		io->input.requestReset = true;
	}
}

void SameBoyProxySystem::loadRom(const Uint8Buffer* romBuffer) {
	SystemIo* io = getStream().get();

	romBuffer->copyTo(&_rom);

	std::cout << "Loading rom.  Found IO: " << (io != nullptr) << std::endl;

	if (io) {
		io->input.romToLoad = std::make_shared<Uint8Buffer>();
		romBuffer->copyTo(io->input.romToLoad.get());
	}
}

bool SameBoyProxySystem::saveSram(Uint8Buffer& target) {
	MemoryAccessor accessor = getMemory(MemoryType::Sram, AccessType::Read);
	if (accessor.isValid()) {
		accessor.getBuffer().copyTo(&target);
		return true;
	}

	return false;
}

bool SameBoyProxySystem::saveState(Uint8Buffer& target) {
	if (_state.size()) {
		_state.copyTo(&target);
		return true;
	}

	return false;
}

MemoryAccessor SameBoyProxySystem::getMemory(MemoryType type, AccessType access) {
	SystemIo* io = getStream().get();

	if (io || access == AccessType::Read) {
		if (type == MemoryType::Rom) {
			return MemoryAccessor(type, _rom.slice(0, _rom.size()), 0, io ? &io->input.patches : nullptr);
		}
		
		if (_state.size()) {
			GB_section_offset_pair_t offset;
			offset.offset = -1;
			offset.size = -1;

			switch (type) {
			case MemoryType::Ram: offset = _stateOffsets.ram; break;
			case MemoryType::Sram: offset = _stateOffsets.mbc; break;
			}

			if (offset.offset != -1) {
				return MemoryAccessor(type, Uint8Buffer(_state.data() + offset.offset, offset.size, false), 0, io ? &io->input.patches : nullptr);
			}
		}
	}

	return MemoryAccessor();
}
