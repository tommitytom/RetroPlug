#pragma once

#include <mutex>
#include <iostream>

class Logger {
private:
	static std::mutex _lock;

public:
	static void log(std::string_view text) {
		std::scoped_lock l(_lock);
		std::cout << text << std::endl;
	}
};

std::mutex Logger::_lock;
