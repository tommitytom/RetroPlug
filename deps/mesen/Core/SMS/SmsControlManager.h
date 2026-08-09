#pragma once
#include "pch.h"
#include "SMS/SmsTypes.h"
#include "Shared/SettingTypes.h"
#include "Shared/BaseControlManager.h"

class SmsConsole;
class SmsVdp;

class SmsControlManager : public BaseControlManager
{
private:
	SmsConsole* _console = nullptr;
	SmsVdp* _vdp = nullptr;
	SmsControlManagerState _state = {};
	SmsConfig _prevConfig = {};
	CvConfig _prevCvConfig = {};

	bool GetTh(bool portB);
	bool GetTr(bool portB);
	uint8_t InternalReadPort(uint8_t port);

	uint8_t ReadColecoVisionPort(uint8_t port);
	void WriteColecoVisionPort(uint8_t value);

	// RetroPlug: host-driven levels on the controller ports, active low, ANDed
	// into InternalReadPort alongside the devices. 0xFF = idle (no-op AND), so
	// this is invisible until a host actually drives a line.
	//
	// Needed because the SMS sync line TH ($DD bit 7) is not reachable through
	// the device model at all: ReadPort(1) takes bit 7 from GetTh(true), which
	// reads InternalReadPort(1) & 0x80, and SmsController::ReadRam(addr == 1)
	// only ever clears bits 0x01/0x02/0x04/0x08. The only stock device that
	// touches addr-1 bit 7 is SmsLightPhaser, and it derives it from VDP
	// scanline and pixel brightness rather than a settable level. Without this
	// a host can drive TR and TL but not TH, and since the tracker ANDs TR and
	// TL into one counter bit, the 2-bit sync counter degenerates to mod-2:
	// half-working sync that reads as an intermittent double-tempo bug.
	//
	// Sits on the manager rather than the devices deliberately, so it survives
	// the per-frame device ClearState() (see UpdateInputState below) and
	// UpdateControlDevices()' ClearDevices()/rebuild.
	uint8_t _extInput[2] = { 0xFF, 0xFF };

public:
	SmsControlManager(Emulator* emu, SmsConsole* console, SmsVdp* vdp);
	shared_ptr<BaseControlDevice> CreateControllerDevice(ControllerType type, uint8_t port) override;

	SmsControlManagerState& GetState() { return _state; }

	// RetroPlug: set the externally-driven level mask for one port. `levels` is
	// active low in the same sense the devices use (bit clear = line asserted),
	// so 0xFF releases every line.
	void SetExternalInput(uint8_t port, uint8_t levels) { _extInput[port & 1] = levels; }
	uint8_t GetExternalInput(uint8_t port) const { return _extInput[port & 1]; }

	void UpdateControlDevices() override;

	// RetroPlug: see the definition. Suppresses the base per-frame input poll,
	// which would otherwise wipe host-set button bits every emulated frame.
	void UpdateInputState() override;

	bool IsPausePressed();

	uint8_t ReadPort(uint8_t port);
	void WriteControlPort(uint8_t value);

	void Serialize(Serializer& s) override;
};