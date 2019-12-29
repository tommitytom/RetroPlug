#pragma once

#include <string>
#include <vector>
#include "util/xstring.h"

template <typename T = char> class DataBuffer;

bool readFile(const tstring& path, std::vector<std::byte>& target);
bool readFile(const tstring& path, DataBuffer<char>* target);
bool readFile(const tstring& path, std::byte* target, size_t size, bool binary = true);
bool readFile(const tstring& path, std::string& target);
bool writeFile(const tstring& path, const std::vector<std::byte>& data);
bool writeFile(const tstring& path, const std::byte* data, size_t size, bool binary = true);
bool writeFile(const tstring& path, const std::string& data);
