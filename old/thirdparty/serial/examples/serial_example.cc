/***
 * This example expects the serial port has a loopback on it.
 *
 * Alternatively, you could use an Arduino:
 *
 * <pre>
 *  void setup() {
 *    Serial.begin(<insert your baudrate here>);
 *  }
 *
 *  void loop() {
 *    if (Serial.available()) {
 *      Serial.write(Serial.read());
 *    }
 *  }
 * </pre>
 */

#include <string>
#include <iostream>
#include <cstdio>

// OS Specific sleep
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "Edio.h"
#include "serial/serial.h"

using std::string;
using std::exception;
using std::cout;
using std::cerr;
using std::endl;
using std::vector;

void my_sleep(unsigned long milliseconds) {
#ifdef _WIN32
      Sleep(milliseconds); // 100 ms
#else
      usleep(milliseconds*1000); // 100 ms
#endif
}

void enumerate_ports()
{
	vector<serial::PortInfo> devices_found = serial::list_ports();

	vector<serial::PortInfo>::iterator iter = devices_found.begin();

	while( iter != devices_found.end() )
	{
		serial::PortInfo device = *iter++;

		printf( "(%s, %s, %s)\n", device.port.c_str(), device.description.c_str(),
     device.hardware_id.c_str() );
	}
}

std::string find_n8_port() {
    std::string id = "USB\\VID_38DF&PID_0017&REV_0200";
    vector<serial::PortInfo> devices_found = serial::list_ports();
    auto found = std::find_if(devices_found.begin(), devices_found.end(), [&id](const serial::PortInfo& device) {
        return device.hardware_id == id;
	});

    if (found != devices_found.end()) {
        return found->port;
    } else {
        throw std::runtime_error("N8 device not found");
	}
}

void print_usage()
{
	cerr << "Usage: test_serial {-e|<serial port address>} ";
    cerr << "<baudrate> [test string]" << endl;
}

int run(int argc, char** argv)
{
    enumerate_ports();

    const uint32_t baud = 9600;
	const string port = find_n8_port();

    Edio edio(port);
	//uint8_t data[] = { 0x90, 0x3C, 0x7F };
    uint8_t data[] = { 0x80, 0x3C, 0x7F };
    edio.memWR(0x1810000, data, 0, 3);

    return 0;
}

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (exception &e) {
    cerr << "Unhandled Exception: " << e.what() << endl;
  }
}
