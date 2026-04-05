#include "Edio.h"
//#include "map_config.h"

#include <cstring>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// Vdc
// ---------------------------------------------------------------------------

Vdc::Vdc(const uint8_t* data) {
	v50 = static_cast<uint16_t>(data[0] | (data[1] << 8));
	v25 = static_cast<uint16_t>(data[2] | (data[3] << 8));
	v12 = static_cast<uint16_t>(data[4] | (data[5] << 8));
	vbt = static_cast<uint16_t>(data[6] | (data[7] << 8));
}

// ---------------------------------------------------------------------------
// RtcTime
// ---------------------------------------------------------------------------

uint8_t RtcTime::decToHex(int val) {
	int hex = 0;
	hex |= (val / 10) << 4;
	hex |= (val % 10);
	return static_cast<uint8_t>(hex);
}

RtcTime::RtcTime(const uint8_t* data) {
	yar = data[0];
	mon = data[1];
	dom = data[2];
	hur = data[3];
	min = data[4];
	sec = data[5];
}

RtcTime::RtcTime(std::time_t t) {
	std::tm* dt = std::localtime(&t);
	yar = decToHex(dt->tm_year + 1900 - 2000);
	mon = decToHex(dt->tm_mon + 1);
	dom = decToHex(dt->tm_mday);
	hur = decToHex(dt->tm_hour);
	min = decToHex(dt->tm_min);
	sec = decToHex(dt->tm_sec);
}

std::vector<uint8_t> RtcTime::getVals() const {
	return { yar, mon, dom, hur, min, sec };
}

void RtcTime::print(const std::string& label) const {
	std::string prefix = label.empty() ? "" : label + " ";
	std::printf("%sdate: %02X.%02X.20%02X\n", prefix.c_str(), dom, mon, yar);
	std::printf("%stime: %02X:%02X:%02X\n", prefix.c_str(), hur, min, sec);
}

// ---------------------------------------------------------------------------
// Edio — construction / connection
// ---------------------------------------------------------------------------

Edio::Edio() {
	std::string port = findN8Port();
	if (port != "") {
		openConnection(port);
	}
}

Edio::Edio(const std::string& portName) {
	openConnection(portName);
}

bool Edio::isValid() const {
	return port && port->isOpen();
}

bool Edio::openConnection(const std::string& portName) {
	try {
		port = std::make_unique<serial::Serial>(
			portName, 9600, serial::Timeout::simpleTimeout(300)
		);
		// Flush anything pending
		port->flushInput();
		getStatus();
		setTimeout(2000);
		return true;
	} catch (const std::exception&) {}

	try {
		if (port && port->isOpen()) port->close();
	} catch (const std::exception&) {}

	port.reset();
	return false;
}

void Edio::setTimeout(uint32_t ms) {
	serial::Timeout timeout = serial::Timeout::simpleTimeout(ms);
	port->setTimeout(timeout);
}

std::string Edio::getPortName() const {
	return port->getPort();
}

// ---------------------------------------------------------------------------
// Low-level TX helpers
// ---------------------------------------------------------------------------

void Edio::tx32(int arg) {
	uint8_t buff[4];
	buff[0] = static_cast<uint8_t>(arg);
	buff[1] = static_cast<uint8_t>(arg >> 8);
	buff[2] = static_cast<uint8_t>(arg >> 16);
	buff[3] = static_cast<uint8_t>(arg >> 24);
	txData(buff, 0, 4);
}

void Edio::tx16(int arg) {
	uint8_t buff[2];
	buff[0] = static_cast<uint8_t>(arg);
	buff[1] = static_cast<uint8_t>(arg >> 8);
	txData(buff, 0, 2);
}

void Edio::tx8(int arg) {
	uint8_t b = static_cast<uint8_t>(arg);
	txData(&b, 0, 1);
}

void Edio::txData(const uint8_t* buff, int offset, int len) {
	while (len > 0) {
		int block = std::min(len, 8192);
		port->write(buff + offset, block);
		offset += block;
		len -= block;
	}
}

void Edio::txData(const std::string& str) {
	port->write(str);
}

void Edio::txDataACK(const uint8_t* buff, int offset, int len) {
	while (len > 0) {
		int resp = rx8();
		if (resp != 0) {
			char msg[64];
			std::snprintf(msg, sizeof(msg), "tx ack: %02X", resp);
			throw std::runtime_error(msg);
		}
		int block = std::min(len, ACK_BLOCK_SIZE);
		txData(buff, offset, block);
		offset += block;
		len -= block;
	}
}

// ---------------------------------------------------------------------------
// Low-level RX helpers
// ---------------------------------------------------------------------------

void Edio::rxData(uint8_t* buff, int offset, int len) {
	int received = 0;
	while (received < len) {
		size_t n = port->read(buff + offset + received, len - received);
		if (n == 0) {
			throw std::runtime_error("rxData timeout");
		}
		received += static_cast<int>(n);
	}
}

std::vector<uint8_t> Edio::rxData(int len) {
	std::vector<uint8_t> buff(len);
	rxData(buff.data(), 0, len);
	return buff;
}

int Edio::rx32() {
	uint8_t buff[4];
	rxData(buff, 0, 4);
	return buff[0] | (buff[1] << 8) | (buff[2] << 16) | (buff[3] << 24);
}

uint16_t Edio::rx16() {
	uint8_t buff[2];
	rxData(buff, 0, 2);
	return static_cast<uint16_t>(buff[0] | (buff[1] << 8));
}

uint8_t Edio::rx8() {
	uint8_t b;
	rxData(&b, 0, 1);
	return b;
}

// ---------------------------------------------------------------------------
// String / FileInfo helpers
// ---------------------------------------------------------------------------

void Edio::txString(const std::string& str) {
	tx16(static_cast<int>(str.size()));
	txData(str);
}

std::string Edio::rxString() {
	int len = rx16();
	auto buff = rxData(len);
	return std::string(buff.begin(), buff.end());
}

FileInfo Edio::rxFileInfo() {
	FileInfo inf;
	inf.size = rx32();
	inf.date = rx16();
	inf.time = rx16();
	inf.attrib = rx8();
	inf.name = rxString();
	return inf;
}

// ---------------------------------------------------------------------------
// Command framing / status
// ---------------------------------------------------------------------------

void Edio::txCMD(uint8_t cmdCode) {
	uint8_t cmd[4];
	cmd[0] = '+';
	cmd[1] = '+' ^ 0xFF;
	cmd[2] = cmdCode;
	cmd[3] = cmdCode ^ 0xFF;
	txData(cmd, 0, 4);
}

int Edio::getStatus() {
	txCMD(CMD_STATUS);
	int resp = rx16();
	if ((resp & 0xFF00) != 0xA500) {
		char msg[64];
		std::snprintf(msg, sizeof(msg), "unexpected status response (%04X)", resp);
		throw std::runtime_error(msg);
	}
	return resp & 0xFF;
}

void Edio::checkStatus() {
	int resp = getStatus();
	if (resp != 0) {
		char msg[64];
		std::snprintf(msg, sizeof(msg), "operation error: %02X", resp);
		throw std::runtime_error(msg);
	}
}

// ---------------------------------------------------------------------------
// Disk
// ---------------------------------------------------------------------------

void Edio::diskInit() {
	txCMD(CMD_DISK_INIT);
	checkStatus();
}

void Edio::diskRead(int addr, uint8_t slen, uint8_t* buff) {
	txCMD(CMD_DISK_RD);
	tx32(addr);
	tx32(slen);

	for (int i = 0; i < slen; i++) {
		uint8_t resp = rx8();
		if (resp != 0) {
			char msg[64];
			std::snprintf(msg, sizeof(msg), "disk read error: %02X", resp);
			throw std::runtime_error(msg);
		}
		rxData(buff, i * 512, 512);
	}
}

// ---------------------------------------------------------------------------
// Directory
// ---------------------------------------------------------------------------

void Edio::dirOpen(const std::string& path) {
	txCMD(CMD_F_DIR_OPN);
	txString(path);
	checkStatus();
}

FileInfo Edio::dirRead(uint16_t maxNameLen) {
	if (maxNameLen == 0) maxNameLen = 0xFFFF;
	txCMD(CMD_F_DIR_RD);
	tx16(maxNameLen);
	int resp = rx8();
	if (resp != 0) {
		char msg[64];
		std::snprintf(msg, sizeof(msg), "dir read error: %02X", resp);
		throw std::runtime_error(msg);
	}
	return rxFileInfo();
}

void Edio::dirLoad(const std::string& path, int sorted) {
	txCMD(CMD_F_DIR_LD);
	tx8(sorted);
	txString(path);
	checkStatus();
}

int Edio::dirGetSize() {
	txCMD(CMD_F_DIR_SIZE);
	return rx16();
}

std::vector<FileInfo> Edio::dirGetRecs(int startIdx, int amount, int maxNameLen) {
	std::vector<FileInfo> inf(amount);
	txCMD(CMD_F_DIR_GET);
	tx16(startIdx);
	tx16(amount);
	tx16(maxNameLen);

	for (int i = 0; i < amount; i++) {
		uint8_t resp = rx8();
		if (resp != 0) {
			char msg[64];
			std::snprintf(msg, sizeof(msg), "dir read error: %02X", resp);
			throw std::runtime_error(msg);
		}
		inf[i] = rxFileInfo();
	}
	return inf;
}

void Edio::dirMake(const std::string& path) {
	txCMD(CMD_F_DIR_MK);
	txString(path);
	int resp = getStatus();
	if (resp != 0 && resp != 8) { // ignore error 8 (already exists)
		checkStatus();
	}
}

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

void Edio::fileOpen(const std::string& path, int mode) {
	txCMD(CMD_F_FOPN);
	tx8(mode);
	txString(path);
	checkStatus();
}

void Edio::fileRead(uint8_t* buff, int offset, int len) {
	txCMD(CMD_F_FRD);
	tx32(len);

	while (len > 0) {
		int block = std::min(len, 4096);
		int resp = rx8();
		if (resp != 0) {
			char msg[64];
			std::snprintf(msg, sizeof(msg), "file read error: %02X", resp);
			throw std::runtime_error(msg);
		}
		rxData(buff, offset, block);
		offset += block;
		len -= block;
	}
}

void Edio::fileRead(int addr, int len) {
	while (len > 0) {
		int block = std::min(len, 0x10000);
		txCMD(CMD_F_FRD_MEM);
		tx32(addr);
		tx32(block);
		tx8(0); // exec
		checkStatus();
		len -= block;
		addr += block;
	}
}

void Edio::fileWrite(const uint8_t* buff, int offset, int len) {
	txCMD(CMD_F_FWR);
	tx32(len);
	txDataACK(buff, offset, len);
	checkStatus();
}

void Edio::fileWrite(int addr, int len) {
	while (len > 0) {
		int block = std::min(len, 0x10000);
		txCMD(CMD_F_FWR_MEM);
		tx32(addr);
		tx32(block);
		tx8(0); // exec
		checkStatus();
		len -= block;
		addr += block;
	}
}

void Edio::fileSetPtr(int addr) {
	txCMD(CMD_F_FPTR);
	tx32(addr);
	checkStatus();
}

void Edio::fileClose() {
	txCMD(CMD_F_FCLOSE);
	checkStatus();
}

FileInfo Edio::fileInfo(const std::string& path) {
	txCMD(CMD_F_FINFO);
	txString(path);
	int resp = rx8();
	if (resp != 0) {
		char msg[64];
		std::snprintf(msg, sizeof(msg), "file access error: %02X", resp);
		throw std::runtime_error(msg);
	}
	return rxFileInfo();
}

uint32_t Edio::fileCRC(int len) {
	txCMD(CMD_F_FCRC);
	tx32(len);
	tx32(0); // crc init val
	int resp = rx8();
	if (resp != 0) {
		char msg[64];
		std::snprintf(msg, sizeof(msg), "Disk read error: %02X", resp);
		throw std::runtime_error(msg);
	}
	return static_cast<uint32_t>(rx32());
}

void Edio::delRecord(const std::string& path) {
	txCMD(CMD_F_DEL);
	txString(path);
	checkStatus();
}

constexpr std::string_view N8_PORT_ID_WIN = "USB\\VID_38DF&PID_0017&REV_0200";
constexpr std::string_view N8_PORT_ID_LIN = "USB VID:PID=38df:0017 SNR=00000000001A";

std::string Edio::findN8Port() {
	std::vector<serial::PortInfo> devicesFound = serial::list_ports();
	auto found = std::find_if(devicesFound.begin(), devicesFound.end(), [](const serial::PortInfo& device) {
#if defined(FW_OS_WINDOWS)
		return device.hardware_id == N8_PORT_ID_WIN;
#else
		return device.hardware_id == N8_PORT_ID_LIN;
#endif
	});

	if (found != devicesFound.end()) {
		return found->port;
	} else {
		return "";
	}
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

void Edio::memWR(int addr, const uint8_t* buff, int offset, int len) {
	if (len == 0) return;
	txCMD(CMD_MEM_WR);
	tx32(addr);
	tx32(len);
	tx8(0); // exec
	txData(buff, offset, len);
}

void Edio::memRD(int addr, uint8_t* buff, int offset, int len) {
	if (len == 0) return;
	txCMD(CMD_MEM_RD);
	tx32(addr);
	tx32(len);
	tx8(0); // exec
	rxData(buff, offset, len);
}

void Edio::memSet(uint8_t val, int addr, int len) {
	txCMD(CMD_MEM_SET);
	tx32(addr);
	tx32(len);
	tx8(val);
	tx8(0); // exec
	checkStatus();
}

bool Edio::memTest(uint8_t val, int addr, int len) {
	txCMD(CMD_MEM_TST);
	tx32(addr);
	tx32(len);
	tx8(val);
	tx8(0); // exec
	return rx8() != 0;
}

uint32_t Edio::memCRC(int addr, int len) {
	txCMD(CMD_MEM_CRC);
	tx32(addr);
	tx32(len);
	tx32(0); // crc init val
	tx8(0);  // exec
	return static_cast<uint32_t>(rx32());
}

void Edio::fifoWR(const uint8_t* data, int offset, int len) {
	memWR(ADDR_FIFO, data, offset, len);
}

void Edio::fifoWR(const std::string& str) {
	auto bytes = reinterpret_cast<const uint8_t*>(str.data());
	memWR(ADDR_FIFO, bytes, 0, static_cast<int>(str.size()));
}

// ---------------------------------------------------------------------------
// Flash
// ---------------------------------------------------------------------------

void Edio::flaRD(int addr, uint8_t* buff, int offset, int len) {
	txCMD(CMD_FLA_RD);
	tx32(addr);
	tx32(len);
	rxData(buff, offset, len);
}

void Edio::flaWR(int addr, const uint8_t* buff, int offset, int len) {
	txCMD(CMD_FLA_WR);
	tx32(addr);
	tx32(len);
	txDataACK(buff, offset, len);
	checkStatus();
}

// ---------------------------------------------------------------------------
// FPGA
// ---------------------------------------------------------------------------
/*
void Edio::fpgInit(const uint8_t* data, int dataLen, MapConfig* cfg) {
	txCMD(CMD_FPG_USB);
	tx32(dataLen);
	txDataACK(data, 0, dataLen);
	checkStatus();
	if (cfg != nullptr) {
		auto bin = cfg->getBinary();
		memWR(ADDR_CFG, bin.data(), 0, static_cast<int>(bin.size()));
	}
}

void Edio::fpgInit(int flashAddr, int size, MapConfig* cfg) {
	txCMD(CMD_FPG_FLA);
	tx32(flashAddr);
	tx32(size);
	tx8(0); // exec
	checkStatus();
	if (cfg != nullptr) {
		auto bin = cfg->getBinary();
		memWR(ADDR_CFG, bin.data(), 0, static_cast<int>(bin.size()));
	}
}

void Edio::fpgInit(const std::string& sdPath, MapConfig* cfg) {
	FileInfo f = fileInfo(sdPath);
	fileOpen(sdPath, FAT_READ);
	txCMD(CMD_FPG_SDC);
	tx32(f.size);
	tx8(0);
	checkStatus();
	if (cfg != nullptr) {
		auto bin = cfg->getBinary();
		memWR(ADDR_CFG, bin.data(), 0, static_cast<int>(bin.size()));
	}
}

void Edio::setConfig(MapConfig& cfg) {
	auto bin = cfg.getBinary();
	memWR(ADDR_CFG, bin.data(), 0, static_cast<int>(bin.size()));
}
*/

// ---------------------------------------------------------------------------
// Mode / RTC / misc
// ---------------------------------------------------------------------------

bool Edio::isServiceMode() {
	txCMD(CMD_GET_MODE);
	uint8_t resp = rx8();
	return resp == 0xA1;
}

Vdc Edio::getVdc() {
	txCMD(CMD_GET_VDC);
	auto buff = rxData(Vdc::SIZE);
	return Vdc(buff.data());
}

RtcTime Edio::rtcGet() {
	txCMD(CMD_RTC_GET);
	auto buff = rxData(RtcTime::SIZE);
	return RtcTime(buff.data());
}

void Edio::rtcSet(std::time_t t) {
	RtcTime rtc(t);
	auto vals = rtc.getVals();
	txCMD(CMD_RTC_SET);
	txData(vals.data(), 0, static_cast<int>(vals.size()));
}

int Edio::rtcCal(std::time_t t, uint8_t arg) {
	RtcTime rtc(t);
	auto vals = rtc.getVals();
	txCMD(CMD_RTC_CAL);
	txData(vals.data(), 0, static_cast<int>(vals.size()));
	tx8(arg);
	return rx32();
}

void Edio::updExec(int addr, int crc) {
	txCMD(CMD_UPD_EXEC);
	tx32(addr);
	tx32(crc);
	tx8(0); // exec
}

// ---------------------------------------------------------------------------
// Service mode / recovery
// ---------------------------------------------------------------------------

void Edio::recovery() {
	if (!isServiceMode()) {
		throw std::runtime_error("Device not in service mode");
	}

	uint8_t crc[4];
	flaRD(ADDR_FLA_ICOR + 4, crc, 0, 4);

	uint32_t oldTimeout = 2000;
	setTimeout(8000);

	txCMD(CMD_USB_RECOV);
	tx32(ADDR_FLA_ICOR);
	txData(crc, 0, 4);
	int status = getStatus();

	setTimeout(oldTimeout);

	if (status == 0x88) {
		throw std::runtime_error("current core matches to recovery copy");
	} else if (status != 0) {
		char msg[64];
		std::snprintf(msg, sizeof(msg), "recovery error: %02X", status);
		throw std::runtime_error(msg);
	}
}

void Edio::exitServiceMode() {
	if (!isServiceMode()) return;
	txCMD(CMD_RUN_APP);
	bootWait();
	if (isServiceMode()) {
		throw std::runtime_error("Device stuck in service mode");
	}
}

void Edio::enterServiceMode() {
	if (isServiceMode()) return;
	txCMD(CMD_HARD_RESET);
	tx8(0);
	bootWait();
	if (!isServiceMode()) {
		throw std::runtime_error("device stuck in APP mode");
	}
}

void Edio::bootWait() {
	for (int i = 0; i < 10; i++) {
		try {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			port->close();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::string name = port->getPort();
			port->open();
			getStatus();
			return;
		} catch (const std::exception&) {}
	}
	throw std::runtime_error("boot timeout");
}
