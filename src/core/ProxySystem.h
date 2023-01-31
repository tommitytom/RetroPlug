#pragma once

#include <entt/core/hashed_string.hpp>

#include "foundation/Event.h"
#include "core/Events.h"
#include "core/System.h"
#include "core/SystemProvider.h"

using namespace entt::literals;

namespace rp {
	class ProxySystem : public System {
	private:
		SystemType _targetType;
		SystemStateOffsets _stateOffsets;
		fw::Uint8Buffer _rom;
		fw::Uint8Buffer _state;
		std::string _romName;
		fw::EventNode& _eventNode;
		bool _gameLink = false;

	public:
		ProxySystem(SystemType type, SystemId id, const std::string& romName, fw::Uint8Buffer&& rom, fw::Uint8Buffer&& state, fw::EventNode& eventNode) :
			System(1),
			_targetType(type),
			_rom(std::move(rom)),
			_state(std::move(state)),
			_romName(romName),
			_eventNode(eventNode)
		{
			setId(id);
		}

		~ProxySystem() = default;

		MemoryAccessor getMemory(MemoryType type, AccessType access) override {
			SystemIo* io = getIo().get();

			if (io || access == AccessType::Read) {
				if (type == MemoryType::Rom) {
					return MemoryAccessor(type, _rom.slice(0, _rom.size()), 0, io ? &io->input.patches : nullptr);
				}

				if (_state.size()) {
					const SystemStateOffset& offset = _stateOffsets[(size_t)type];

					if (offset.size) {
						return MemoryAccessor(type, fw::Uint8Buffer(_state.data() + offset.offset, offset.size, false), 0, io ? &io->input.patches : nullptr);
					}
				}
			}

			return MemoryAccessor();
		}

		void setResolution(fw::DimensionU32 res) {
			_resolution = res;
		}

		bool load(LoadConfig&& loadConfig) override {
			setDesc(loadConfig.desc);
			_eventNode.send("Audio"_hs, LoadEvent{ .systemId = getId(), .config = std::move(loadConfig) });
			return true; 
		}

		void reset() override {
			_eventNode.send("Audio"_hs, ResetSystemEvent{ .systemId = getId() });
		}

		void setStateBuffer(fw::Uint8Buffer&& buffer) override {
			_state = std::move(buffer);
		}

		void process(uint32 frameCount) override {
			if (_stream->output.video) {
				_frameBuffer = *_stream->output.video;
			}
		}

		void setSampleRate(uint32 sampleRate) override {}

		bool saveState(fw::Uint8Buffer& target) override {
			if (_state.size()) {
				_state.copyTo(&target);
				return true;
			}

			return false;
		}

		bool saveSram(fw::Uint8Buffer& target) override { 
			MemoryAccessor accessor = getMemory(MemoryType::Sram, AccessType::Read);
			if (accessor.isValid()) {
				accessor.getBuffer().copyTo(&target);
				return true;
			}

			return false;
		}

		std::string getRomName() override { return _romName; }

		bool isProxy() const override { return true; }

		SystemType getTargetType() const override {
			return _targetType;
		}

		void setGameLink(bool gameLink) override {
			_gameLink = gameLink;
			_eventNode.send("Audio"_hs, SetGameLinkEvent{ .systemId = getId(), .enabled = gameLink});
		}

		bool getGameLink() override { return _gameLink; }

		void addLinkTarget(System* system) override {}

		void removeLinkTarget(System* system) override {}
	};

	using ProxySystemPtr = std::shared_ptr<ProxySystem>;

	class ProxyProvider final : public SystemProvider {
	public:
		SystemType getType() const override { return 1; }

		SystemPtr createSystem() const override { return nullptr; }

		bool canLoadRom(std::string_view path) const override { return false; }

		bool canLoadSram(std::string_view path) const override { return false; }

		std::string getRomName(const fw::Uint8Buffer& romData) const override { return ""; }
	};
}
