#pragma once

#include <algorithm>
#include <queue>
#include <vector>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <functional>

#if defined(_WIN32)
#include <processthreadsapi.h>
#else
#include <unistd.h>
#endif

#include "Core/NES/INesMemoryHandler.h"

// EverDrive N8 Pro FIFO emulator. Maps to NES address space at $40F0 (data)
// and $40F1 (status). The N8-midi ROM polls $40F1 bit 7 (`FIFO_MOS_RXF`):
// set = no data, clear = data ready. Bytes pushed via `pushByte` (host-MIDI
// bytes for n8-midi) become available to the ROM on the next `lda $40F0`.
//
// Both directions are real. On the device, `$40F0` reads drain fifo_a (host→NES)
// and `$40F0` writes fill fifo_b (NES→host), which the MCU consumes as a stream
// of framed Edio commands (fpga/base_sv/base_io.sv). Most of those commands ask
// the MCU for something and get a reply queued back into fifo_a — the SD-card
// file API below. `CMD_USB_WR` is the exception that carries data the other way:
// the MCU forwards its payload to the USB host, which is how a cartridge talks
// back, and here it queues for `drainTx`.
//
// `CMD_F_FRD_MEM` is the exception in a different direction: instead of replying
// through the FIFO, the MCU reads the open file straight into CARTRIDGE memory
// over the PI bus, with the 6502 doing nothing but waiting. It needs a third
// register, `$40FF` (REG_MSTAT), and a two-step trigger: the parameters STAGE the
// transfer and a later `$40F0` write EXECUTES it (see execStagedDma). Proven on a
// real N8 Pro from a running game: 8192 bytes into CHR-RAM, byte-exact, at roughly
// a hundred times the rate of the CPU-mediated `CMD_F_FRD` path.
//
// Register addresses confirmed against old/bliptoaster/rom/everdrive.h. The
// legacy MesenComponents.h had a stale 0x4150/0x4151 — the values here
// are correct.

namespace rp {

	// Diagnostic: set RP_FIFO_TRACE=1 to log the exact byte stream the ROM
	// reads at $40F0, host-MIDI pushes, and per-command response sizes. Used to
	// investigate the N8 MIDI "priming" behaviour.
	inline bool fifoTraceEnabled() {
		static const bool on = (std::getenv("RP_FIFO_TRACE") != nullptr);
		return on;
	}

	// This process's id, for naming the default SD-card scratch directory. Concurrent harness runs
	// (the reaper suite, a consumer's per-file test processes) must not share a card.
	inline long long currentProcessId() {
#if defined(_WIN32)
		return static_cast<long long>(::GetCurrentProcessId());
#else
		return static_cast<long long>(::getpid());
#endif
	}

	// -----------------------------------------------------------------------
	// Command codes (NES SDK / Edio protocol)
	// -----------------------------------------------------------------------
	static constexpr uint8_t CMD_STATUS     = 0x10;
	static constexpr uint8_t CMD_FPG_CFG    = 0x21;
	// The three NES→host writes. Each is `u16 len` + `len` bytes. USB_WR asks the MCU to forward the
	// payload verbatim to the USB host (edn8-pro-pub edio/everdrive.c `ed_cmd_usb_wr`; the host end is
	// `edlink usbrd`, and here `drainTx`) — the cartridge CAN talk back, over the same fifo_b the Edio
	// command parser reads (fpga/base_sv/base_io.sv). It is how the N8's own OS streams its VRAM back
	// for a menu screenshot. FIFO_WR loops the payload into the cart's own RX buffer, so the ROM reads
	// its own bytes at $40F0. UART_WR targets a physical serial port this emulation has no counterpart
	// for — parsed and dropped, because an UNparsed payload would desync the command parser.
	static constexpr uint8_t CMD_USB_WR     = 0x22;
	static constexpr uint8_t CMD_FIFO_WR    = 0x23;
	static constexpr uint8_t CMD_UART_WR    = 0x24;
	static constexpr uint8_t CMD_DISK_INIT  = 0xC0;
	static constexpr uint8_t CMD_F_DIR_LD   = 0xC5;
	static constexpr uint8_t CMD_F_DIR_SIZE = 0xC6;
	static constexpr uint8_t CMD_F_DIR_GET  = 0xC8;
	static constexpr uint8_t CMD_F_FOPN    = 0xC9;
	static constexpr uint8_t CMD_F_FRD     = 0xCA;
	// addr(4) + len(4), both little-endian u32: read from the open file straight into cartridge
	// memory by DMA. `edio/everdrive.c` ed_cmd_file_read_mem sends the parameters, then runs the
	// halt handshake below rather than reading a reply out of the FIFO.
	static constexpr uint8_t CMD_F_FRD_MEM = 0xCB;
	static constexpr uint8_t CMD_F_FWR     = 0xCC;
	static constexpr uint8_t CMD_F_FCLOSE  = 0xCE;
	static constexpr uint8_t CMD_F_FPTR    = 0xCF;
	static constexpr uint8_t CMD_F_FINFO   = 0xD0;
	static constexpr uint8_t CMD_F_DIR_MK  = 0xD2;
	static constexpr uint8_t CMD_F_DEL     = 0xD3;

	// FatFs open-mode flags, as the NES SDK passes them to CMD_F_FOPN.
	static constexpr uint8_t FA_READ         = 0x01;
	static constexpr uint8_t FA_WRITE        = 0x02;
	static constexpr uint8_t FA_CREATE_ALWAYS = 0x08;
	static constexpr uint8_t FS_MAKEPATH     = 0x80;  // create missing parent directories

	// CMD_F_FWR's flow-control granularity (everdrive.c ACK_BLOCK_SIZE): the device sends one ack byte,
	// the ROM answers with up to this many payload bytes, and so on until the length is met.
	static constexpr uint32_t ACK_BLOCK_SIZE = 1024;

	// -----------------------------------------------------------------------
	// $40FF (REG_MSTAT) - the mapper status register the DMA handshake polls
	// -----------------------------------------------------------------------
	// Reads answer `MSTAT_BASE | pendBits | strobe`, and the strobe flips on EVERY read: the SDK's
	// wait loop reads the register twice per iteration and requires the two to differ in nothing but
	// that bit, which is how it knows the register is live rather than open bus. Writes set the
	// pending bits, which is how a ROM arms the operation it is about to wait on.
	static constexpr uint16_t REG_MSTAT      = 0x40FF;
	static constexpr uint8_t  MSTAT_BASE     = 0xA0;  // the loop also checks the high nibble reads $A
	static constexpr uint8_t  MSTAT_MCU_PEND = 0x02;
	static constexpr uint8_t  MSTAT_FPG_PEND = 0x04;
	static constexpr uint8_t  MSTAT_PEND_MASK = MSTAT_MCU_PEND | MSTAT_FPG_PEND;
	static constexpr uint8_t  MSTAT_STROBE   = 0x08;

	// -----------------------------------------------------------------------
	// PI bus geography of the N8 Pro cartridge (mirrors packages/retroplug/src/n8/edio.ts)
	// -----------------------------------------------------------------------
	// The addresses CMD_F_FRD_MEM's first parameter is drawn from. Only the CHR-RAM window is
	// backed here; see setCartWriter for why the others are refused rather than guessed at.
	static constexpr uint32_t PI_ADDR_PRG = 0x0000000;  // PRG PSRAM (8 MB)
	static constexpr uint32_t PI_ADDR_CHR = 0x0800000;  // CHR PSRAM (8 MB)
	static constexpr uint32_t PI_ADDR_SRM = 0x1000000;  // cart battery RAM
	// everdrive.sv forces bit 22 of the CHR address for a chr_ram game, so a CHR-RAM cart's RAM is
	// the UPPER 4 MB of the CHR chip and not ADDR_CHR - the same fact `n8-load --dump-chr` encodes.
	static constexpr uint32_t PI_CHR_RAM_OFFSET = 0x400000;
	static constexpr uint32_t PI_ADDR_CHR_RAM   = PI_ADDR_CHR + PI_CHR_RAM_OFFSET;  // 0xC00000

	// How fast the MCU moves bytes over the PI bus, for the wait-time model (see setCartClock).
	//
	// DERIVED FROM THE ONE HARDWARE MEASUREMENT, and re-derivable if that ROM's loop changes:
	// nesvj's wait loop (its src/core/dma.s) is 37 CPU cycles per spin, and a real N8 Pro took 165
	// and 172 spins to move 8192 bytes over two boots. That is ~6.2k cycles, or ~3.7 ms against
	// PAL's 1662607 Hz, so ~2.2 MB/s - inside the 1.7-2.8 MB/s band that run reported.
	//
	// Quoted as bytes per second rather than cycles per byte on purpose: the MCU's transfer takes a
	// fixed WALL time, and a faster CPU simply burns more cycles waiting for it. So the same number
	// holds on NTSC, where a cycles-per-byte constant would have made the transfer 8% quicker.
	static constexpr double DMA_BYTES_PER_SECOND = 2.2e6;

	// The largest transfer any PI destination could absorb: the device's biggest chip is 8 MB. A
	// longer request is a garbled parameter rather than a big read, and it has to be refused BEFORE
	// the staging buffer is sized - a u32 length taken at face value would allocate up to 4 GB.
	static constexpr uint32_t DMA_MAX_LEN = 8u * 1024 * 1024;

	// -----------------------------------------------------------------------
	// FIFO fidelity profile: how closely the host->NES queue behaves like the cartridge's
	// -----------------------------------------------------------------------
	// The default is PERMISSIVE - unbounded and delivered instantly - because that is what every
	// existing test was written against. The device is neither, and the difference hides whole
	// classes of bug: a ROM that can never lose a byte never has its recovery path exercised, and
	// one fed a whole chunk atomically is never starved mid-structure the way a real wire starves it.
	// A test opts in to the hardware profile through the "mesen" role's `fifo` field.
	//
	// The measured constants, kept here rather than in a shell script:
	//   depth  - SIZE_FIFO in edn8-pro-pub/edio/everdrive.h. nesvj's EDIO_BLOCK of 1792 is chosen
	//            against it (the reply is the block PLUS a resp byte, so 2048 cannot serve 2048).
	//   rate   - nesvj measured 512 bytes arriving over ~3.4 ms on the USB link, so ~150 KB/s. This
	//            is the BURST rate of the wire, not a sustained throughput: a host that paces its
	//            chunks is modelling its own gaps, and that pacing is the test's business.
	static constexpr uint32_t FIFO_HW_DEPTH            = 2048;
	static constexpr uint32_t FIFO_HW_BYTES_PER_SECOND = 150000;

	// -----------------------------------------------------------------------
	// Host-side directory record (matches ed_rx_file_info layout)
	// -----------------------------------------------------------------------
	struct EdioDirRecord {
		uint32_t size   = 0;
		uint16_t date   = 0;
		uint16_t time   = 0;
		uint8_t  attrib = 0;   // bit0 = AT_DIR
		std::string name;
	};

	// -----------------------------------------------------------------------
	class NesEverdriveFifo : public INesMemoryHandler {
	private:
		// ----- RX queue (emulator → NES) --------------------------------
		std::queue<uint8_t> _rxQueue;
		std::mutex _mutex;

		// ----- TX queue (NES → host) ------------------------------------
		// What the ROM handed to CMD_USB_WR, waiting for the host to drain (drainTx). The real MCU
		// forwards these straight out of the USB port; a harness reads them here instead. Deliberately
		// NOT cleared by clearRx/flushAll — those are barriers in the host→NES direction only.
		std::vector<uint8_t> _txQueue;

		// ----- TX parser state (NES → emulator) -------------------------
		enum class ParseState {
			WaitHeader0,  // waiting for '+'
			WaitHeader1,  // waiting for '+'^0xFF
			WaitCmd,      // waiting for command byte
			WaitCmdInv,   // waiting for cmd^0xFF
			CollectParams,   // accumulating parameter bytes for the current command
			CollectWriteData // accumulating CMD_F_FWR payload, one ack-gated block at a time
		};

		ParseState  _parseState    = ParseState::WaitHeader0;
		uint8_t     _currentCmd    = 0;
		std::vector<uint8_t> _params;
		size_t      _paramBytesNeeded = 0;

		// CMD_F_FWR block state: bytes still owed for the whole write, and the block being collected.
		// The payload arrives OUTSIDE the command frame (the ROM interleaves it with our ack bytes), so
		// it needs a parser state of its own rather than a parameter continuation.
		uint32_t             _writeRemaining = 0;
		std::vector<uint8_t> _writeBlock;

		// Callback set after partial param collection (e.g. for string reads)
		std::function<size_t(const std::vector<uint8_t>&)> _paramContinuation;

		// ----- Filesystem state -----------------------------------------
		std::filesystem::path _sdRoot;       // maps "/" on the N8 SD card
		std::filesystem::path _openFilePath;
		std::fstream          _openFile;
		uint32_t              _filePtr = 0;

		// Loaded directory listing (from CMD_F_DIR_LD)
		std::vector<EdioDirRecord> _dirRecords;

		// ----- Edio command status --------------------------------------
		// Real N8 Edio commands (FOPN/FCLOSE/DIR_LD/…) do NOT auto-emit a
		// status word; they set an internal result that the ROM retrieves with
		// a *separate* CMD_STATUS query (the SDK's `ed_check_status`). We mirror
		// that: commands store `_lastStatus`, and only CMD_STATUS emits it.
		// Auto-emitting here instead (the old behaviour) left the CMD_STATUS
		// reply's 2 bytes unread in the RX FIFO — and the high byte 0xA5 is a
		// valid MIDI status (Poly-Aftertouch, ch5), which desynced n8-midi's
		// MIDI parser and caused the "first message is ignored" priming quirk.
		uint8_t _lastStatus = 0;

		// ----- The wire (host -> MCU -> fifo_a) -------------------------
		// What the host has handed over but the wire has not yet carried. Unbounded on purpose: it
		// stands for the host's own outbound buffer, which has no cartridge counterpart and must not
		// drop - a byte lost HERE would be the emulation inventing a failure the device never has.
		// Bytes cross into _rxQueue at _wireBytesPerSecond (see pumpWire), and only there can they be
		// dropped, by the queue being full.
		std::queue<uint8_t> _wireQueue;
		uint32_t _wireBytesPerSecond = 0;   // 0 = instant: the wire is not modelled
		uint64_t _wireLastCycle      = 0;
		double   _wireCredit         = 0.0; // fractional bytes carried over between pumps

		// fifo_a's depth, and what it has cost. 0 = unbounded (the permissive default).
		uint32_t _rxCapacity   = 0;
		uint64_t _droppedBytes = 0;         // cumulative, never cleared by a read: a test samples it

		// ----- CMD_F_FRD_MEM / $40FF handshake --------------------------
		// Where a DMA writes, and how the emulation learns time has passed. Both injected: the FIFO
		// knows the Edio protocol, not which host buffer stands in for the cartridge.
		std::function<bool(uint32_t, const uint8_t*, size_t)> _cartWriter;
		std::function<uint64_t()> _cartCycles;
		uint32_t _cartCpuClockHz = 0;

		uint8_t  _pendBits = 0;  // $40FF bits 1-2, set by a write, cleared as the MCU finishes
		uint8_t  _strobe   = 0;  // $40FF bit 3, flipped on every read
		// The staged (parameters received, not yet executed) transfer. See execStagedDma for why
		// those are two steps and not one.
		bool     _dmaStaged = false;
		uint32_t _dmaAddr   = 0;
		uint32_t _dmaLen    = 0;
		// Executed, still inside its modelled transfer window. Distinct from "the pending bit is
		// set", which a ROM can also do by arming $40FF with no transfer behind it.
		bool     _dmaInFlight = false;
		// Cycle count at which the modelled transfer finishes and MSTAT_MCU_PEND clears. 0 = the
		// transfer is instant (no clock source installed).
		uint64_t _dmaDoneAtCycle = 0;

	public:
		// Set the host directory that stands in for the SD card root ("/"). Must be set before the ROM
		// runs any SD command; MesenNesSystem does it at activate from the "mesen" role's `sdRoot`.
		//
		// Empty means "no card was named", which resolves to a per-process scratch directory (see
		// defaultSdRoot) rather than the process's working directory. Nothing is created for it — a
		// missing directory already reads as an empty card — so a NES system that never touches SD
		// leaves nothing on disk.
		void setSdRoot(const std::filesystem::path& root) {
			std::lock_guard<std::mutex> lock(_mutex);
			_sdRoot = root;
		}

		// Where CMD_F_FRD_MEM's DMA lands: write `len` bytes into cartridge memory at a PI bus
		// address, answering false if that range is not backed. MesenNesSystem installs one at
		// activate, mapping the CHR-RAM window (PI_ADDR_CHR_RAM) onto Mesen's NesChrRam.
		//
		// The whole range MUST be validated before a single byte is written. PRG, SRAM and CHR-ROM
		// are legal PI targets on the device but have no verified emulated counterpart, and a
		// silent drop would look EXACTLY like a working DMA to the ROM - the worst failure mode
		// available here - so an unbacked address is an Edio error, not a no-op.
		void setCartWriter(std::function<bool(uint32_t piAddr, const uint8_t* data, size_t len)> fn) {
			std::lock_guard<std::mutex> lock(_mutex);
			_cartWriter = std::move(fn);
		}

		// Give the cartridge a sense of time: `cycleSource` reads the CPU's cycle counter and
		// `cpuClockHz` converts a modelled wall duration into cycles. Two things use it - the DMA's
		// transfer window (DMA_BYTES_PER_SECOND) and the host wire's delivery rate (setFifoProfile).
		//
		// WITHOUT it both are instant: a DMA completes on the wait loop's first pass and a pushed
		// byte is readable immediately. Correct, but it makes anything measured against it optimistic.
		void setCartClock(std::function<uint64_t()> cycleSource, uint32_t cpuClockHz) {
			std::lock_guard<std::mutex> lock(_mutex);
			_cartCycles = std::move(cycleSource);
			_cartCpuClockHz = cpuClockHz;
			_wireLastCycle = _cartCycles ? _cartCycles() : 0;
		}

		// How faithfully the host->NES queue behaves like fifo_a. `depth` 0 = unbounded,
		// `bytesPerSecond` 0 = instant delivery; both together are the permissive default every
		// existing test was written against. MesenNesSystem resolves the "mesen" role's `fifo`
		// profile (and any explicit override) into this pair at activate.
		//
		// A rate needs setCartClock to have been called, or there is no clock to meter against and
		// delivery stays instant. A depth does not: dropping is a queue property, not a timing one.
		void setFifoProfile(uint32_t depth, uint32_t bytesPerSecond) {
			std::lock_guard<std::mutex> lock(_mutex);
			_rxCapacity = depth;
			_wireBytesPerSecond = bytesPerSecond;
			_wireCredit = 0.0;
			_wireLastCycle = _cartCycles ? _cartCycles() : 0;
		}

		// Depth of each stage, and what the queue has cost so far. `droppedBytes` is CUMULATIVE and
		// a read does not clear it, so a timeline can sample it repeatedly without racing itself.
		struct FifoStats {
			uint64_t rxDepth      = 0;  // delivered, waiting for the ROM to read
			uint64_t rxCapacity   = 0;  // 0 = unbounded
			uint64_t droppedBytes = 0;  // lost to a full queue, for the whole run
			uint64_t wirePending  = 0;  // handed over by the host, not yet carried
			uint64_t txDepth      = 0;  // NES->host (CMD_USB_WR), waiting for drainTx
		};

		FifoStats stats() {
			std::lock_guard<std::mutex> lock(_mutex);
			pumpWire();
			return FifoStats{ _rxQueue.size(), _rxCapacity, _droppedBytes, _wireQueue.size(), _txQueue.size() };
		}

		// Drop the DMA handshake: any staged-but-unexecuted transfer, the pending bits and the
		// wait-time deadline. Called wherever the ROM restarts (core reset, savestate load) - a
		// staged DMA that outlived a reset would swallow the rebooted ROM's next $40F0 write as an
		// exec trigger. Deliberately NOT part of flushAll, whose contract is a barrier in the
		// host->NES byte direction only.
		void clearDmaHandshake() {
			std::lock_guard<std::mutex> lock(_mutex);
			_dmaStaged = false;
			_dmaInFlight = false;
			_dmaAddr = 0;
			_dmaLen = 0;
			_dmaDoneAtCycle = 0;
			_pendBits = 0;
		}

		// ----------------------------------------------------------------
		// INesMemoryHandler
		// ----------------------------------------------------------------
		void GetMemoryRanges(MemoryRanges& ranges) override {
			ranges.SetAllowOverride();
			ranges.AddHandler(MemoryOperation::Read,  0x40F0, 0x40F1);
			ranges.AddHandler(MemoryOperation::Write, 0x40F0, 0x40F1);
			// $40FF is the mapper status register, in the same $4020-$5FFF space a mapper would
			// otherwise own. Overriding one more address there is the trade already made for
			// $40F0/$40F1, and on an N8-hosted ROM it is the faithful answer.
			ranges.AddHandler(MemoryOperation::Read,  REG_MSTAT);
			ranges.AddHandler(MemoryOperation::Write, REG_MSTAT);
		}

		uint8_t ReadRam(uint16_t addr) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (addr == REG_MSTAT) {
				retireDmaIfElapsed();
				const uint8_t val = MSTAT_BASE | _pendBits | _strobe;
				_strobe ^= MSTAT_STROBE;   // per READ, not per poll iteration: the loop reads twice
				return val;
			}
			// Both registers carry the wire forward first: the ROM's poll of $40F1 is exactly the
			// moment a byte that has had time to arrive should have arrived.
			pumpWire();
			if (addr == 0x40F1) {
				return _rxQueue.empty() ? 0x80 : 0x00;
			}
			if (addr == 0x40F0) {
				if (!_rxQueue.empty()) {
					uint8_t val = _rxQueue.front();
					_rxQueue.pop();
					if (fifoTraceEnabled())
						std::fprintf(stderr, "[fifo] rd  %02X (rem=%zu)\n", val, _rxQueue.size());
					return val;
				}
				return 0xFF;
			}
			return 0xFF;
		}

		uint8_t PeekRam(uint16_t addr) override {
			std::lock_guard<std::mutex> lock(_mutex);
			// The debugger's peek path: same value, no strobe flip and no DMA retirement. A viewer
			// refreshing $40FF must not be able to satisfy the ROM's wait loop on its behalf.
			if (addr == REG_MSTAT) return static_cast<uint8_t>(MSTAT_BASE | _pendBits | _strobe);
			if (addr == 0x40F1) return _rxQueue.empty() ? 0x80 : 0x00;
			if (addr == 0x40F0) return _rxQueue.empty() ? 0xFF : _rxQueue.front();
			return 0xFF;
		}

		void WriteRam(uint16_t addr, uint8_t value) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (addr == REG_MSTAT) {
				// Arming: the ROM sets the bits it is about to wait on.
				_pendBits = value & MSTAT_PEND_MASK;
				return;
			}
			if (addr != 0x40F0) return;
			// Carry the wire forward BEFORE the command runs. A command's reply is pushed straight
			// into fifo_a, so without this a host byte that had already arrived would be overtaken by
			// a reply generated after it and the ROM would read the two out of order. The wire is a
			// delay, never a reordering.
			pumpWire();
			// A $40F0 write with a transfer staged is the exec trigger (the SDK's _ed_halt_exec),
			// not command data. Feeding it to the parser instead would both lose the DMA and leave
			// the ROM waiting on a pending bit nothing would ever clear.
			if (_dmaStaged) {
				execStagedDma();
				return;
			}
			parseByte(value);
		}

		// ----------------------------------------------------------------
		// Called from MIDI callback or audio thread
		// ----------------------------------------------------------------
		// Hand one byte to the WIRE, not to the ROM. Under the default instant profile the next pump
		// carries it straight through and this is what it always was; with a rate set, it arrives
		// when the wire has had time to carry it.
		void pushByte(uint8_t byte) {
			std::lock_guard<std::mutex> lock(_mutex);
			// Carry what is already owed before adding to the queue, so an idle gap between pushes is
			// the host's gap rather than credit the wire banks up and then bursts through.
			pumpWire();
			_wireQueue.push(byte);
			if (fifoTraceEnabled())
				std::fprintf(stderr, "[fifo] midi %02X (wire=%zu depth=%zu)\n",
				             byte, _wireQueue.size(), _rxQueue.size());
		}

		// Number of bytes waiting in the RX queue (not yet read by the ROM). For tests / introspection.
		// Bytes still on the wire are NOT counted - they have not been delivered. See stats().
		std::size_t rxCount() {
			std::lock_guard<std::mutex> lock(_mutex);
			pumpWire();
			return _rxQueue.size();
		}

		// Drop every byte the ROM has not read: both the DELIVERED ones sitting in fifo_a and the
		// ones still crossing the wire. A host-sync arm is a barrier - the ROM must not read clocks
		// queued for the position it just left - and a byte mid-wire is every bit as stale as a
		// delivered one. Leaves the TX parser state alone: only the emulator->NES direction is being
		// re-pointed.
		void clearRx() {
			std::lock_guard<std::mutex> lock(_mutex);
			std::queue<uint8_t> empty;
			_rxQueue.swap(empty);
			std::queue<uint8_t> emptyWire;
			_wireQueue.swap(emptyWire);
			_wireCredit = 0.0;
			_wireLastCycle = _cartCycles ? _cartCycles() : 0;
		}

		// Take everything the ROM has sent host-ward via CMD_USB_WR since the last drain, oldest byte
		// first, and clear it. The emulated twin of reading the N8's USB port while a game runs (the
		// `retroplug-n8-hwtest fiford` op on real hardware). Empty when the ROM has sent nothing.
		std::vector<uint8_t> drainTx() {
			std::lock_guard<std::mutex> lock(_mutex);
			std::vector<uint8_t> out;
			out.swap(_txQueue);
			return out;
		}

		// Bytes waiting in the TX queue (not yet drained by the host). For tests / introspection.
		std::size_t txCount() {
			std::lock_guard<std::mutex> lock(_mutex);
			return _txQueue.size();
		}

	private:
		// ----------------------------------------------------------------
		// fifo_a: delivery and depth - called with _mutex held
		// ----------------------------------------------------------------

		// The ONLY way a byte enters the ROM's read queue, whether it came off the wire or from the
		// MCU answering an Edio command. On the device those share one 2048-byte fifo_a, so the depth
		// applies to both: a command reply landing while the ROM is behind on reading is exactly the
		// case nesvj's EDIO_BLOCK of 1792 is sized to avoid.
		//
		// A full queue DROPS, and counts. Byte-granular, which is the honest model of "no room": the
		// losses nesvj measured on hardware came in exact multiples of 2048, but that is a host/MCU
		// accounting artefact of the USB link, and reproducing the number by fitting to it would be
		// inventing a mechanism rather than modelling one.
		void pushRx(uint8_t byte) {
			if (_rxCapacity != 0 && _rxQueue.size() >= _rxCapacity) {
				++_droppedBytes;
				if (fifoTraceEnabled())
					std::fprintf(stderr, "[fifo] DROP %02X (full at %u, dropped=%llu)\n",
					             byte, _rxCapacity, static_cast<unsigned long long>(_droppedBytes));
				return;
			}
			_rxQueue.push(byte);
		}

		// Carry bytes from the host's outbound buffer into fifo_a at the modelled wire rate. Without
		// a rate or a clock this moves everything, which is the historical behaviour: a pushed byte is
		// readable on the ROM's very next poll.
		//
		// Credit is accumulated in fractional bytes so a rate that is not a whole number of bytes per
		// cycle does not round down to nothing on every short interval. It resets when the wire runs
		// dry: an idle wire carries nothing, so time spent with nothing to send must not bank up into
		// a burst when the host next writes.
		void pumpWire() {
			if (_wireQueue.empty()) {
				_wireCredit = 0.0;
				_wireLastCycle = _cartCycles ? _cartCycles() : _wireLastCycle;
				return;
			}
			if (_wireBytesPerSecond == 0 || !_cartCycles || _cartCpuClockHz == 0) {
				while (!_wireQueue.empty()) { pushRx(_wireQueue.front()); _wireQueue.pop(); }
				return;
			}

			const uint64_t now = _cartCycles();
			if (now > _wireLastCycle) {
				const double bytesPerCycle =
					static_cast<double>(_wireBytesPerSecond) / static_cast<double>(_cartCpuClockHz);
				_wireCredit += static_cast<double>(now - _wireLastCycle) * bytesPerCycle;
			}
			_wireLastCycle = now;

			while (_wireCredit >= 1.0 && !_wireQueue.empty()) {
				pushRx(_wireQueue.front());
				_wireQueue.pop();
				_wireCredit -= 1.0;
			}
			if (_wireQueue.empty()) _wireCredit = 0.0;
		}

		// ----------------------------------------------------------------
		// TX parser — called with _mutex held
		// ----------------------------------------------------------------
		void parseByte(uint8_t b) {
			switch (_parseState) {
			case ParseState::WaitHeader0:
				if (b == '+') _parseState = ParseState::WaitHeader1;
				break;

			case ParseState::WaitHeader1:
				if (b == ('+' ^ 0xFF)) _parseState = ParseState::WaitCmd;
				else                   _parseState = ParseState::WaitHeader0;
				break;

			case ParseState::WaitCmd:
				_currentCmd = b;
				_parseState = ParseState::WaitCmdInv;
				break;

			case ParseState::WaitCmdInv:
				if (b == (_currentCmd ^ 0xFF)) {
					_params.clear();
					_paramContinuation = nullptr;
					beginCommand(_currentCmd);
				} else {
					_parseState = ParseState::WaitHeader0;
				}
				break;

			case ParseState::CollectParams:
				_params.push_back(b);
				if (_paramContinuation) {
					size_t more = _paramContinuation(_params);
					if (more == 0) {
						executeCommand(_currentCmd);
						if (_parseState == ParseState::CollectParams)
							_parseState = ParseState::WaitHeader0;
					}
					// else: continuation updated _paramBytesNeeded via the lambda
				} else if (_params.size() >= _paramBytesNeeded) {
					executeCommand(_currentCmd);
					// A command may hand the parser on rather than end it (CMD_F_FWR → CollectWriteData);
					// only return to the header scan if it didn't.
					if (_parseState == ParseState::CollectParams)
						_parseState = ParseState::WaitHeader0;
				}
				break;

			case ParseState::CollectWriteData:
				collectWriteByte(b);
				break;
			}
		}

		// CMD_F_FWR payload, one ack-gated block at a time. The ROM has already been sent an ack; it
		// answers with up to ACK_BLOCK_SIZE bytes, which land in the open file. Another ack follows for
		// as long as bytes are owed, then a stored status the ROM collects with its own CMD_STATUS.
		void collectWriteByte(uint8_t b) {
			_writeBlock.push_back(b);
			const uint32_t block = std::min(_writeRemaining, ACK_BLOCK_SIZE);
			if (_writeBlock.size() < block) return;

			if (_openFile.is_open()) {
				_openFile.seekp(_filePtr);
				_openFile.write(reinterpret_cast<const char*>(_writeBlock.data()),
				                static_cast<std::streamsize>(_writeBlock.size()));
				_openFile.flush();
			}
			_filePtr += static_cast<uint32_t>(_writeBlock.size());
			_writeRemaining -= block;
			_writeBlock.clear();

			if (_writeRemaining > 0) {
				pushRx(0x00);  // ack the next block
				return;
			}
			setStatus(_openFile.is_open() ? uint8_t(0) : uint8_t(0x04));  // FAT_NO_FILE if never opened
			_parseState = ParseState::WaitHeader0;
		}

		// ----------------------------------------------------------------
		// Decide how many parameter bytes to collect before executing
		// ----------------------------------------------------------------
		void beginCommand(uint8_t cmd) {
			switch (cmd) {
			// No parameters — execute immediately. Reset the parser to WaitHeader0
			// so the NEXT command's header ('+') is recognised — otherwise the
			// parser is left mid-command (WaitCmdInv) and the following command
			// (e.g. any op after the ubiquitous CMD_STATUS query) is dropped.
			case CMD_STATUS:
			case CMD_DISK_INIT:
			case CMD_F_FCLOSE:
			case CMD_F_DIR_SIZE:
				_paramBytesNeeded = 0;
				executeCommand(cmd);
				_parseState = ParseState::WaitHeader0;
				return;

			// Fixed-size parameters
			case CMD_F_FPTR:    _paramBytesNeeded = 4; break; // u32 addr
			case CMD_F_FRD:     _paramBytesNeeded = 4; break; // u32 len
			case CMD_F_FRD_MEM: _paramBytesNeeded = 8; break; // u32 dest PI addr + u32 len
			case CMD_F_FWR:     _paramBytesNeeded = 4; break; // u32 len (then ACK-data follows separately)

			// CMD_F_DIR_LD: 1 byte (sorted) + length-prefixed string
			case CMD_F_DIR_LD:
				_paramBytesNeeded = 3; // sorted(1) + str_len(2)
				_paramContinuation = [this](const std::vector<uint8_t>& p) -> size_t {
					if (p.size() < 3) return 1;
					uint16_t strLen = static_cast<uint16_t>(p[1] | (p[2] << 8));
					size_t needed = 3u + strLen;
					if (p.size() < needed) return needed - p.size();
					_paramContinuation = nullptr;
					return 0; // done
				};
				break;

			// CMD_F_DIR_GET: u16 start + u16 amount + u16 maxNameLen
			case CMD_F_DIR_GET:
				_paramBytesNeeded = 6;
				break;

			// CMD_F_FOPN: 1 byte mode + length-prefixed string
			case CMD_F_FOPN:
				_paramBytesNeeded = 3; // mode(1) + str_len(2)
				_paramContinuation = [this](const std::vector<uint8_t>& p) -> size_t {
					if (p.size() < 3) return 1;
					uint16_t strLen = static_cast<uint16_t>(p[1] | (p[2] << 8));
					size_t needed = 3u + strLen;
					if (p.size() < needed) return needed - p.size();
					_paramContinuation = nullptr;
					return 0;
				};
				break;

			// The NES→host writes: u16 len + len bytes, all framed the same way.
			case CMD_USB_WR:
			case CMD_FIFO_WR:
			case CMD_UART_WR:
				_paramBytesNeeded = 2; // len(2)
				_paramContinuation = [this](const std::vector<uint8_t>& p) -> size_t {
					if (p.size() < 2) return 1;
					uint16_t len = static_cast<uint16_t>(p[0] | (p[1] << 8));
					size_t needed = 2u + len;
					if (p.size() < needed) return needed - p.size();
					_paramContinuation = nullptr;
					return 0;
				};
				break;

			// CMD_F_FINFO: length-prefixed string
			case CMD_F_FINFO:
				_paramBytesNeeded = 2;
				_paramContinuation = [this](const std::vector<uint8_t>& p) -> size_t {
					if (p.size() < 2) return 1;
					uint16_t strLen = static_cast<uint16_t>(p[0] | (p[1] << 8));
					size_t needed = 2u + strLen;
					if (p.size() < needed) return needed - p.size();
					_paramContinuation = nullptr;
					return 0;
				};
				break;

			// CMD_F_DIR_MK / CMD_F_DEL: length-prefixed string
			case CMD_F_DIR_MK:
			case CMD_F_DEL:
				_paramBytesNeeded = 2;
				_paramContinuation = [this](const std::vector<uint8_t>& p) -> size_t {
					if (p.size() < 2) return 1;
					uint16_t strLen = static_cast<uint16_t>(p[0] | (p[1] << 8));
					size_t needed = 2u + strLen;
					if (p.size() < needed) return needed - p.size();
					_paramContinuation = nullptr;
					return 0;
				};
				break;

			// CMD_FPG_CFG: consumes 40 bytes, no meaningful response needed
			case CMD_FPG_CFG:
				_paramBytesNeeded = 40;
				break;

			default:
				// Unknown command — ignore; wait for next header
				_parseState = ParseState::WaitHeader0;
				return;
			}

			_parseState = ParseState::CollectParams;
		}

		// ----------------------------------------------------------------
		// Execute a fully-received command
		// ----------------------------------------------------------------
		void executeCommand(uint8_t cmd) {
			const std::size_t before = _rxQueue.size();
			switch (cmd) {
			case CMD_STATUS:      execStatus();    break;
			case CMD_DISK_INIT:   execDiskInit();  break;
			case CMD_F_DIR_LD:    execDirLoad();   break;
			case CMD_F_DIR_SIZE:  execDirSize();   break;
			case CMD_F_DIR_GET:   execDirGet();    break;
			case CMD_F_FOPN:      execFileOpen();  break;
			case CMD_F_FRD:       execFileRead();  break;
			case CMD_F_FRD_MEM:   stageFileReadMem(); break;
			case CMD_F_FWR:       execFileWrite(); break;
			case CMD_F_FCLOSE:    execFileClose(); break;
			case CMD_F_FPTR:      execFileSetPtr(); break;
			case CMD_F_FINFO:     execFileInfo();  break;
			case CMD_F_DIR_MK:    execDirMake();   break;
			case CMD_F_DEL:       execFileDel();   break;
			case CMD_USB_WR:      execUsbWrite();  break;
			case CMD_FIFO_WR:     execFifoWrite(); break;
			case CMD_UART_WR:     /* no emulated serial port — parsed above, payload dropped */ break;
			case CMD_FPG_CFG:     /* init stub — no response */ break;
			default: break;
			}
			if (fifoTraceEnabled())
				std::fprintf(stderr, "[fifo] cmd=%02X resp+=%zu (depth=%zu)\n",
				             cmd, _rxQueue.size() - before, _rxQueue.size());
		}

		// ----------------------------------------------------------------
		// Helpers
		// ----------------------------------------------------------------

		// Record the result of the last Edio command. The ROM reads it later via
		// a CMD_STATUS query (execStatus) — commands must NOT emit it themselves.
		void setStatus(uint8_t errorCode) {
			_lastStatus = errorCode;
		}

		// Push a 16-bit status word: 0xA500 | errorCode. Only emitted in reply to
		// a CMD_STATUS query (see setStatus's rationale).
		void pushStatus(uint8_t errorCode = 0) {
			pushRx(static_cast<uint8_t>(errorCode)); // low byte
			pushRx(0xA5);                            // high byte
		}

		void pushU16(uint16_t v) {
			pushRx(static_cast<uint8_t>(v));
			pushRx(static_cast<uint8_t>(v >> 8));
		}

		void pushU32(uint32_t v) {
			pushRx(static_cast<uint8_t>(v));
			pushRx(static_cast<uint8_t>(v >> 8));
			pushRx(static_cast<uint8_t>(v >> 16));
			pushRx(static_cast<uint8_t>(v >> 24));
		}

		void pushString(const std::string& s) {
			pushU16(static_cast<uint16_t>(s.size()));
			for (uint8_t c : s) pushRx(c);
		}

		// Read a length-prefixed string out of _params at a given offset.
		// Returns the string and advances offset past it.
		std::string readParamString(size_t offset) {
			if (offset + 2 > _params.size()) return {};
			uint16_t len = static_cast<uint16_t>(_params[offset] | (_params[offset + 1] << 8));
			offset += 2;
			if (offset + len > _params.size()) return {};
			return std::string(reinterpret_cast<const char*>(_params.data() + offset), len);
		}

		uint32_t readParamU32(size_t offset) {
			if (offset + 4 > _params.size()) return 0;
			return static_cast<uint32_t>(
				_params[offset] |
				(_params[offset + 1] << 8) |
				(_params[offset + 2] << 16) |
				(_params[offset + 3] << 24));
		}

		uint16_t readParamU16(size_t offset) {
			if (offset + 2 > _params.size()) return 0;
			return static_cast<uint16_t>(_params[offset] | (_params[offset + 1] << 8));
		}

		// Where an unset SD root points: a per-process scratch directory, NOT the working directory.
		// The CWD made the emulated card depend on where the CLI happened to be started, and let a file
		// a test wrote land next to the source, where it silently became every later run's "card".
		// Computed once; nothing creates it until a ROM actually writes.
		static const std::filesystem::path& defaultSdRoot() {
			static const std::filesystem::path root =
				std::filesystem::temp_directory_path() /
				("retroplug-sd-" + std::to_string(static_cast<long long>(currentProcessId())));
			return root;
		}

		// Convert an N8 path (absolute, e.g. "/music/song.lsdj") to a host path.
		std::filesystem::path toHostPath(const std::string& nesPath) {
			// Strip leading slash so that it is relative to the card root
			std::string rel = nesPath;
			if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
			return (_sdRoot.empty() ? defaultSdRoot() : _sdRoot) / rel;
		}

		// Build an EdioDirRecord from a directory_entry
		static EdioDirRecord recordFromEntry(const std::filesystem::directory_entry& de) {
			EdioDirRecord r;
			r.name   = de.path().filename().string();
			r.attrib = de.is_directory() ? 0x10 : 0x00;
			if (!de.is_directory()) {
				std::error_code ec;
				r.size = static_cast<uint32_t>(de.file_size(ec));
			}
			return r;
		}

		// Push a single EdioDirRecord into _rxQueue (ed_rx_file_info layout).
		void pushFileInfo(const EdioDirRecord& r) {
			pushU32(r.size);
			pushU16(r.date);
			pushU16(r.time);
			pushRx(r.attrib);
			pushString(r.name);
		}

		// ----------------------------------------------------------------
		// Command implementations
		// ----------------------------------------------------------------

		void execStatus() {
			// Emit the stored result of the last command (0 if none yet).
			pushStatus(_lastStatus);
		}

		void execDiskInit() {
			// Always succeeds for the host filesystem
			setStatus(0);
		}

		void execDirLoad() {
			// params: sorted(1) + str_len(2) + path(str_len)
			// uint8_t sorted = _params[0]; // (ignored — we sort alphabetically)
			std::string nesPath = readParamString(1);
			std::filesystem::path dir = toHostPath(nesPath);

			_dirRecords.clear();

			std::error_code ec;
			if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
				setStatus(0x05); // FAT_NO_PATH
				return;
			}

			for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
				_dirRecords.push_back(recordFromEntry(entry));
			}

			std::sort(_dirRecords.begin(), _dirRecords.end(), [](const EdioDirRecord& a, const EdioDirRecord& b) {
				return a.name < b.name;
			});

			setStatus(0);
		}

		void execDirSize() {
			pushU16(static_cast<uint16_t>(_dirRecords.size()));
		}

		void execDirGet() {
			// params: u16 startIdx, u16 amount, u16 maxNameLen
			uint16_t startIdx   = readParamU16(0);
			uint16_t amount     = readParamU16(2);
			uint16_t maxNameLen = readParamU16(4);

			for (uint16_t i = 0; i < amount; i++) {
				size_t idx = startIdx + i;
				if (idx >= _dirRecords.size()) {
					pushRx(0x04); // FAT_NO_FILE — signals end of listing
					break;
				}

				pushRx(0x00); // resp == 0 means record follows

				EdioDirRecord r = _dirRecords[idx];
				if (maxNameLen > 0 && r.name.size() > maxNameLen) {
					r.name = r.name.substr(0, maxNameLen);
				}
				pushFileInfo(r);
			}
		}

		void execFileOpen() {
			// params: mode(1) + str_len(2) + path(str_len)
			uint8_t mode = _params[0];
			std::string nesPath = readParamString(1);

			if (nesPath.empty()) {
				setStatus(0x03); // ERR_NULL_PATH
				return;
			}

			_openFilePath = toHostPath(nesPath);
			_filePtr = 0;

			std::ios::openmode flags = std::ios::binary;
			if (mode & FA_READ)  flags |= std::ios::in;
			if (mode & FA_WRITE) flags |= std::ios::out;
			// FA_CREATE_ALWAYS truncates (or creates) rather than requiring the file to exist. Without
			// it an `in|out` open of a missing file fails, which is why a write-open never used to work.
			if (mode & FA_CREATE_ALWAYS) flags |= std::ios::trunc;

			_openFile.close();
			_openFile.clear();  // an earlier failed open leaves failbit set, which poisons the next one

			// FS_MAKEPATH: create the parent directories the way the device's FatFs layer does. Also the
			// only thing that materialises the default scratch card — a read-only ROM never creates it.
			if ((mode & FS_MAKEPATH) && (mode & FA_WRITE)) {
				std::error_code ec;
				if (const auto parent = _openFilePath.parent_path(); !parent.empty())
					std::filesystem::create_directories(parent, ec);
			}

			_openFile.open(_openFilePath, flags);
			// `in|out` without trunc still needs the file to exist; a plain write-open of a new file is
			// legitimate, so retry it as a create.
			if (!_openFile.is_open() && (mode & FA_WRITE)) {
				_openFile.clear();
				_openFile.open(_openFilePath, flags | std::ios::trunc);
			}

			if (!_openFile.is_open()) {
				setStatus(0x04); // FAT_NO_FILE
				return;
			}

			setStatus(0);
		}

		void execFileRead() {
			// params: u32 len. ONE resp byte, then `len` bytes — the framing ed_cmd_file_read expects.
			// The SDK issues a separate command per ≤512-byte block ("we can read up to 4096 in a single
			// block, but not recommended, to avoid fifo overload"), reading exactly one resp for each.
			// Emitting a resp per 512 bytes WITHIN one command is identical at exactly 512 and desyncs
			// the ROM's decode above it.
			uint32_t len = readParamU32(0);
			if (len == 0) {
				pushRx(0x00);
				return;
			}

			std::vector<uint8_t> buf(len, 0x00);
			uint32_t got = 0;
			if (_openFile.is_open()) {
				_openFile.clear();  // a prior read to EOF leaves eofbit set, which fails the next seek
				_openFile.seekg(_filePtr);
				_openFile.read(reinterpret_cast<char*>(buf.data()), len);
				got = static_cast<uint32_t>(_openFile.gcount());
				_filePtr += got;
			}

			// Nothing to give (no open file, or already at EOF) is an ERROR, not a success with no data:
			// a resp of 0 followed by no bytes leaves a polling ROM waiting for a payload forever.
			if (got == 0) {
				pushRx(0x04); // FAT_NO_FILE
				return;
			}

			pushRx(0x00); // success resp
			for (uint32_t i = 0; i < got; i++) pushRx(buf[i]);
		}

		// CMD_F_FRD_MEM: STAGE the transfer, do NOT perform it. The ROM arms $40FF AFTER sending
		// the parameters and only then writes the exec byte, so a copy here would clear the pending
		// bit before the arming write set it - leaving nothing to ever clear it again. The ROM
		// would spin to its timeout and report failure for a DMA that had actually happened.
		void stageFileReadMem() {
			// params: u32 destination PI address, then u32 length.
			_dmaAddr   = readParamU32(0);
			_dmaLen    = readParamU32(4);
			_dmaStaged = true;
		}

		// The exec write landed ($40F0 with a transfer staged): move the bytes, then start the
		// clock the ROM's wait loop is actually waiting on.
		void execStagedDma() {
			_dmaStaged = false;
			setStatus(performDma(_dmaAddr, _dmaLen));

			// MSTAT_MCU_PEND clears when the modelled transfer time elapses, whether or not the
			// transfer succeeded: "the MCU answered" and "it did what was asked" are separate
			// questions, and the ROM asks the second with CMD_STATUS. MSTAT_FPG_PEND is left alone,
			// since no emulated operation drives the FPGA half of the handshake.
			_dmaInFlight    = true;
			_dmaDoneAtCycle = 0;
			if (_cartCycles && _cartCpuClockHz > 0 && _dmaLen > 0) {
				const double seconds = static_cast<double>(_dmaLen) / DMA_BYTES_PER_SECOND;
				_dmaDoneAtCycle = _cartCycles() +
					static_cast<uint64_t>(seconds * static_cast<double>(_cartCpuClockHz));
			}
			retireDmaIfElapsed();
		}

		// Clear MSTAT_MCU_PEND once the modelled transfer window has passed. With no clock source
		// the deadline is 0 and the DMA retires immediately, so a host with no CPU to ask (every
		// protocol test, and any future non-Mesen caller) still sees a coherent handshake.
		//
		// Only an EXECUTED transfer retires. A ROM that arms $40FF without one waits forever, which
		// is what the device does too.
		void retireDmaIfElapsed() {
			if (!_dmaInFlight) return;
			if (_dmaDoneAtCycle != 0 && _cartCycles && _cartCycles() < _dmaDoneAtCycle) return;
			_dmaInFlight    = false;
			_dmaDoneAtCycle = 0;
			_pendBits &= static_cast<uint8_t>(~MSTAT_MCU_PEND);
		}

		// Move `len` bytes from the open file at _filePtr into cartridge memory at `piAddr`, and
		// advance the file pointer exactly as CMD_F_FRD does - a ROM that interleaves the two
		// against one pointer (a small header over the FIFO, the bulk by DMA) depends on that.
		// Returns the Edio status the ROM will collect with its CMD_STATUS query.
		uint8_t performDma(uint32_t piAddr, uint32_t len) {
			if (len == 0) return 0;
			if (len > DMA_MAX_LEN) return 0x13;     // garbled parameter (FR_INVALID_PARAMETER)
			if (!_cartWriter) return 0x13;          // no backing memory at all
			if (!_openFile.is_open()) return 0x04;  // FAT_NO_FILE

			std::vector<uint8_t> buf(len, 0x00);
			_openFile.clear();  // a prior read to EOF leaves eofbit set, which fails the next seek
			_openFile.seekg(_filePtr);
			_openFile.read(reinterpret_cast<char*>(buf.data()), len);
			const uint32_t got = static_cast<uint32_t>(_openFile.gcount());
			// Already at EOF is an ERROR, not a success that moved nothing, by the same reasoning
			// as execFileRead: a ROM told "fine" about an empty transfer reads stale pixels forever.
			if (got == 0) return 0x04;

			// The writer validates the whole range before writing a byte, so a refusal leaves both
			// cartridge memory and the file pointer untouched.
			if (!_cartWriter(piAddr, buf.data(), got)) return 0x13;

			_filePtr += got;
			return 0;
		}

		void execFileWrite() {
			// params: u32 len; the payload follows OUTSIDE the command frame, ack-gated. ed_cmd_file_write
			// reads ONE ack, sends up to ACK_BLOCK_SIZE bytes, reads the next ack, and so on — so the acks
			// must be issued one at a time as each block lands. Pushing them all up front (the old
			// behaviour) let the ROM send its whole payload straight into the command parser, where it
			// decoded as garbage commands and nothing was ever written.
			uint32_t len = readParamU32(0);
			if (len == 0) {
				setStatus(_openFile.is_open() ? uint8_t(0) : uint8_t(0x04));
				return;
			}

			_writeRemaining = len;
			_writeBlock.clear();
			_writeBlock.reserve(std::min(len, ACK_BLOCK_SIZE));
			pushRx(0x00);  // ack the first block; collectWriteByte takes it from here
			_parseState = ParseState::CollectWriteData;
		}

		// CMD_USB_WR: hand the payload to the host. On the device the MCU forwards it out of the USB
		// port; here it queues for drainTx. params: len(2) + payload.
		void execUsbWrite() {
			const uint16_t len = readParamU16(0);
			if (_params.size() < 2u + len) return;
			_txQueue.insert(_txQueue.end(), _params.begin() + 2, _params.begin() + 2 + len);
			if (fifoTraceEnabled())
				std::fprintf(stderr, "[fifo] usb_wr %u (txDepth=%zu)\n", len, _txQueue.size());
		}

		// CMD_FIFO_WR ("write to own fifo buffer"): the payload loops straight back into the RX queue,
		// so the ROM reads its own bytes at $40F0. params: len(2) + payload.
		void execFifoWrite() {
			const uint16_t len = readParamU16(0);
			if (_params.size() < 2u + len) return;
			for (uint16_t i = 0; i < len; i++) pushRx(_params[2 + i]);
		}

		void execFileClose() {
			_openFile.close();
			_openFile.clear();
			_writeRemaining = 0;
			_writeBlock.clear();
			setStatus(0);
		}

		void execFileSetPtr() {
			// params: u32 addr
			_filePtr = readParamU32(0);
			_openFile.clear();  // a read that hit EOF leaves eofbit set, and a seek on it fails
			_openFile.seekg(_filePtr);
			_openFile.seekp(_filePtr);
			setStatus(0);
		}

		void execFileInfo() {
			// params: str_len(2) + path(str_len)
			std::string nesPath = readParamString(0);
			std::filesystem::path hostPath = toHostPath(nesPath);

			std::error_code ec;
			if (!std::filesystem::exists(hostPath, ec)) {
				pushRx(0x04); // FAT_NO_FILE — resp byte before info
				return;
			}

			pushRx(0x00); // resp == 0 → info follows
			std::filesystem::directory_entry de(hostPath, ec);
			EdioDirRecord r = recordFromEntry(de);
			pushFileInfo(r);
		}

		void execDirMake() {
			// params: str_len(2) + path(str_len)
			std::string nesPath = readParamString(0);
			std::filesystem::path hostPath = toHostPath(nesPath);

			std::error_code ec;
			std::filesystem::create_directories(hostPath, ec);
			setStatus(ec ? uint8_t(0x07) : uint8_t(0));
		}

		void execFileDel() {
			// params: str_len(2) + path(str_len)
			std::string nesPath = readParamString(0);
			std::filesystem::path hostPath = toHostPath(nesPath);

			std::error_code ec;
			std::filesystem::remove(hostPath, ec);
			setStatus(ec ? uint8_t(0x04) : uint8_t(0));
		}
	};
}
