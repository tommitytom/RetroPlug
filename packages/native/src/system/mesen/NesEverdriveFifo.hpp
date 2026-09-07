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

		// ----------------------------------------------------------------
		// INesMemoryHandler
		// ----------------------------------------------------------------
		void GetMemoryRanges(MemoryRanges& ranges) override {
			ranges.SetAllowOverride();
			ranges.AddHandler(MemoryOperation::Read,  0x40F0, 0x40F1);
			ranges.AddHandler(MemoryOperation::Write, 0x40F0, 0x40F1);
		}

		uint8_t ReadRam(uint16_t addr) override {
			std::lock_guard<std::mutex> lock(_mutex);
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
			if (addr == 0x40F1) return _rxQueue.empty() ? 0x80 : 0x00;
			if (addr == 0x40F0) return _rxQueue.empty() ? 0xFF : _rxQueue.front();
			return 0xFF;
		}

		void WriteRam(uint16_t addr, uint8_t value) override {
			if (addr != 0x40F0) return;
			std::lock_guard<std::mutex> lock(_mutex);
			parseByte(value);
		}

		// ----------------------------------------------------------------
		// Called from MIDI callback or audio thread
		// ----------------------------------------------------------------
		void pushByte(uint8_t byte) {
			std::lock_guard<std::mutex> lock(_mutex);
			_rxQueue.push(byte);
			if (fifoTraceEnabled())
				std::fprintf(stderr, "[fifo] midi %02X (depth=%zu)\n", byte, _rxQueue.size());
		}

		// Number of bytes waiting in the RX queue (not yet read by the ROM). For tests / introspection.
		std::size_t rxCount() {
			std::lock_guard<std::mutex> lock(_mutex);
			return _rxQueue.size();
		}

		// Drop every DELIVERED-but-unread byte. A host-sync arm is a barrier: the ROM must not read
		// clocks queued for the position it just left, and those bytes are already past the pending
		// queue and sitting here. Leaves the TX parser state alone - only the emulator->NES direction
		// is being re-pointed.
		void clearRx() {
			std::lock_guard<std::mutex> lock(_mutex);
			std::queue<uint8_t> empty;
			_rxQueue.swap(empty);
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
				_rxQueue.push(0x00);  // ack the next block
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
			_rxQueue.push(static_cast<uint8_t>(errorCode)); // low byte
			_rxQueue.push(0xA5);                            // high byte
		}

		void pushU16(uint16_t v) {
			_rxQueue.push(static_cast<uint8_t>(v));
			_rxQueue.push(static_cast<uint8_t>(v >> 8));
		}

		void pushU32(uint32_t v) {
			_rxQueue.push(static_cast<uint8_t>(v));
			_rxQueue.push(static_cast<uint8_t>(v >> 8));
			_rxQueue.push(static_cast<uint8_t>(v >> 16));
			_rxQueue.push(static_cast<uint8_t>(v >> 24));
		}

		void pushString(const std::string& s) {
			pushU16(static_cast<uint16_t>(s.size()));
			for (uint8_t c : s) _rxQueue.push(c);
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
			_rxQueue.push(r.attrib);
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
					_rxQueue.push(0x04); // FAT_NO_FILE — signals end of listing
					break;
				}

				_rxQueue.push(0x00); // resp == 0 means record follows

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
				_rxQueue.push(0x00);
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
				_rxQueue.push(0x04); // FAT_NO_FILE
				return;
			}

			_rxQueue.push(0x00); // success resp
			for (uint32_t i = 0; i < got; i++) _rxQueue.push(buf[i]);
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
			_rxQueue.push(0x00);  // ack the first block; collectWriteByte takes it from here
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
			for (uint16_t i = 0; i < len; i++) _rxQueue.push(_params[2 + i]);
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
				_rxQueue.push(0x04); // FAT_NO_FILE — resp byte before info
				return;
			}

			_rxQueue.push(0x00); // resp == 0 → info follows
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
