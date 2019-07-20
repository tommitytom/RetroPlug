#pragma once

#include <string>

class RetroPlug;

void serialize(std::string& target, const RetroPlug& manager);

void deserialize(const char* data, RetroPlug& plug);
