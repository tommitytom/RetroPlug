#pragma once

#include <string>
#include <vector>

bool readFile(const std::string& path, std::vector<std::byte>& target);
bool readFile(const std::string& path, std::byte* target, size_t size, bool binary = true);
bool readFile(const std::string& path, std::string& target);
bool writeFile(const std::string& path, const std::vector<std::byte>& data);
bool writeFile(const std::string& path, const std::byte* data, size_t size, bool binary = true);
bool writeFile(const std::string& path, const std::string& data);

bool readFile(const std::wstring& path, std::vector<std::byte>& target);
bool readFile(const std::wstring& path, std::byte* target, size_t size, bool binary = true);
bool readFile(const std::wstring& path, std::string& target);
bool writeFile(const std::wstring& path, const std::vector<std::byte>& data);
bool writeFile(const std::wstring& path, const std::byte* data, size_t size, bool binary = true);
bool writeFile(const std::wstring& path, const std::string& data);