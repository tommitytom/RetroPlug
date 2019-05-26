#pragma once

#include <string>
#include <vector>

bool readFile(const std::string& path, std::vector<char>& target);
bool readFile(const std::string& path, char* target, size_t size);
bool writeFile(const std::string& path, const std::vector<char>& data);
bool writeFile(const std::string& path, const char* data, size_t size);