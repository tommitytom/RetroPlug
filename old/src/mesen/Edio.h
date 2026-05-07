#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <memory>
#include <serial/serial.h>

struct FileInfo {
	std::string name;
	int size = 0;
	uint16_t date = 0;
	uint16_t time = 0;
	uint8_t attrib = 0;
};

struct Vdc {
	static constexpr int SIZE = 8;
	uint16_t v50;
	uint16_t v25;
	uint16_t v12;
	uint16_t vbt;

	explicit Vdc(const uint8_t* data);
};

struct RtcTime {
	static constexpr int SIZE = 6;
	uint8_t yar;
	uint8_t mon;
	uint8_t dom;
	uint8_t hur;
	uint8_t min;
	uint8_t sec;

	explicit RtcTime(const uint8_t* data);
	explicit RtcTime(std::time_t t);

	std::vector<uint8_t> getVals() const;
	void print(const std::string& label = "RTC") const;

private:
	static uint8_t decToHex(int val);
};

// Forward declaration
class MapConfig;

class Edio {
public:
	static constexpr int ACK_BLOCK_SIZE = 1024;

	static constexpr int ADDR_PRG = 0x0000000;
	static constexpr int ADDR_CHR = 0x0800000;
	static constexpr int ADDR_SRM = 0x1000000;
	static constexpr int ADDR_CFG = 0x1800000;
	static constexpr int ADDR_SSR = 0x1802000;
	static constexpr int ADDR_FIFO = 0x1810000;
	static constexpr int ADDR_FLA_MENU = 0x00000;
	static constexpr int ADDR_FLA_FPGA = 0x40000;
	static constexpr int ADDR_FLA_ICOR = 0x80000;

	static constexpr int SIZE_PRG = 0x800000;
	static constexpr int SIZE_CHR = 0x800000;
	static constexpr int SIZE_SRM = 0x40000;

	static constexpr int ADDR_MENU_PRG = (ADDR_PRG + 0x7E0000);
	static constexpr int ADDR_MENU_CHR = (ADDR_CHR + 0x7E0000);

	static constexpr uint8_t FAT_READ = 0x01;
	static constexpr uint8_t FAT_WRITE = 0x02;
	static constexpr uint8_t FAT_OPEN_EXISTING = 0x00;
	static constexpr uint8_t FAT_CREATE_NEW = 0x04;
	static constexpr uint8_t FAT_CREATE_ALWAYS = 0x08;
	static constexpr uint8_t FAT_OPEN_ALWAYS = 0x10;
	static constexpr uint8_t FAT_OPEN_APPEND = 0x30;

	// Auto-seek constructor
	Edio();

	// Explicit port constructor
	explicit Edio(const std::string& portName);

	~Edio() = default;

	std::string getPortName() const;

	// Public protocol methods
	int getStatus();
	void diskInit();
	void diskRead(int addr, uint8_t slen, uint8_t* buff);

	void dirOpen(const std::string& path);
	FileInfo dirRead(uint16_t maxNameLen = 0);
	void dirLoad(const std::string& path, int sorted);
	int dirGetSize();
	std::vector<FileInfo> dirGetRecs(int startIdx, int amount, int maxNameLen);
	void dirMake(const std::string& path);

	void fileOpen(const std::string& path, int mode);
	void fileRead(uint8_t* buff, int offset, int len);
	void fileRead(int addr, int len);
	void fileWrite(const uint8_t* buff, int offset, int len);
	void fileWrite(int addr, int len);
	void fileSetPtr(int addr);
	void fileClose();
	FileInfo fileInfo(const std::string& path);
	uint32_t fileCRC(int len);

	void delRecord(const std::string& path);

	void memWR(int addr, const uint8_t* buff, int offset, int len);
	void memRD(int addr, uint8_t* buff, int offset, int len);
	void memSet(uint8_t val, int addr, int len);
	bool memTest(uint8_t val, int addr, int len);
	uint32_t memCRC(int addr, int len);

	void fifoWR(const uint8_t* data, int offset, int len);
	void fifoWR(const std::string& str);

	void flaRD(int addr, uint8_t* buff, int offset, int len);
	void flaWR(int addr, const uint8_t* buff, int offset, int len);

	void fpgInit(const uint8_t* data, int dataLen, MapConfig* cfg);
	void fpgInit(int flashAddr, int size, MapConfig* cfg);
	void fpgInit(const std::string& sdPath, MapConfig* cfg);

	void setConfig(MapConfig& cfg);
	// MapConfig getConfig(); // requires MapConfig definition

	bool isServiceMode();
	Vdc getVdc();
	RtcTime rtcGet();
	void rtcSet(std::time_t t);
	int rtcCal(std::time_t t, uint8_t arg);

	void updExec(int addr, int crc);
	void recovery();
	void exitServiceMode();
	void enterServiceMode();

	// Expose raw rx for advanced use
	int rx32();
	uint16_t rx16();
	uint8_t rx8();

	static std::string findN8Port();
	bool isValid() const;

private:
	static constexpr uint8_t CMD_STATUS = 0x10;
	static constexpr uint8_t CMD_GET_MODE = 0x11;
	static constexpr uint8_t CMD_HARD_RESET = 0x12;
	static constexpr uint8_t CMD_GET_VDC = 0x13;
	static constexpr uint8_t CMD_RTC_GET = 0x14;
	static constexpr uint8_t CMD_RTC_SET = 0x15;
	static constexpr uint8_t CMD_FLA_RD = 0x16;
	static constexpr uint8_t CMD_FLA_WR = 0x17;
	static constexpr uint8_t CMD_FLA_WR_SDC = 0x18;
	static constexpr uint8_t CMD_MEM_RD = 0x19;
	static constexpr uint8_t CMD_MEM_WR = 0x1A;
	static constexpr uint8_t CMD_MEM_SET = 0x1B;
	static constexpr uint8_t CMD_MEM_TST = 0x1C;
	static constexpr uint8_t CMD_MEM_CRC = 0x1D;
	static constexpr uint8_t CMD_FPG_USB = 0x1E;
	static constexpr uint8_t CMD_FPG_SDC = 0x1F;
	static constexpr uint8_t CMD_FPG_FLA = 0x20;
	static constexpr uint8_t CMD_RTC_CAL = 0x21;
	static constexpr uint8_t CMD_USB_WR = 0x22;
	static constexpr uint8_t CMD_FIFO_WR = 0x23;
	static constexpr uint8_t CMD_UART_WR = 0x24;
	static constexpr uint8_t CMD_REINIT = 0x25;
	static constexpr uint8_t CMD_SYS_INF = 0x26;
	static constexpr uint8_t CMD_GAME_CTR = 0x27;
	static constexpr uint8_t CMD_UPD_EXEC = 0x28;

	static constexpr uint8_t CMD_DISK_INIT = 0xC0;
	static constexpr uint8_t CMD_DISK_RD = 0xC1;
	static constexpr uint8_t CMD_DISK_WR = 0xC2;
	static constexpr uint8_t CMD_F_DIR_OPN = 0xC3;
	static constexpr uint8_t CMD_F_DIR_RD = 0xC4;
	static constexpr uint8_t CMD_F_DIR_LD = 0xC5;
	static constexpr uint8_t CMD_F_DIR_SIZE = 0xC6;
	static constexpr uint8_t CMD_F_DIR_PATH = 0xC7;
	static constexpr uint8_t CMD_F_DIR_GET = 0xC8;
	static constexpr uint8_t CMD_F_FOPN = 0xC9;
	static constexpr uint8_t CMD_F_FRD = 0xCA;
	static constexpr uint8_t CMD_F_FRD_MEM = 0xCB;
	static constexpr uint8_t CMD_F_FWR = 0xCC;
	static constexpr uint8_t CMD_F_FWR_MEM = 0xCD;
	static constexpr uint8_t CMD_F_FCLOSE = 0xCE;
	static constexpr uint8_t CMD_F_FPTR = 0xCF;
	static constexpr uint8_t CMD_F_FINFO = 0xD0;
	static constexpr uint8_t CMD_F_FCRC = 0xD1;
	static constexpr uint8_t CMD_F_DIR_MK = 0xD2;
	static constexpr uint8_t CMD_F_DEL = 0xD3;

	static constexpr uint8_t CMD_USB_RECOV = 0xF0;
	static constexpr uint8_t CMD_RUN_APP = 0xF1;

	std::unique_ptr<serial::Serial> port;

	bool openConnection(const std::string& portName);
	void setTimeout(uint32_t ms);
	void bootWait();

	// Low-level TX/RX
	void tx32(int arg);
	void tx16(int arg);
	void tx8(int arg);
	void txData(const uint8_t* buff, int offset, int len);
	void txData(const std::string& str);
	void txDataACK(const uint8_t* buff, int offset, int len);
	void rxData(uint8_t* buff, int offset, int len);
	std::vector<uint8_t> rxData(int len);

	void txString(const std::string& str);
	std::string rxString();
	FileInfo rxFileInfo();

	void txCMD(uint8_t cmdCode);
	void checkStatus();
};
