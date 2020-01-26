#pragma once

#include <string_view>

class Resource {
private:
	std::string _resourceClass;
	int _resourceId = -1;
	void* _resourceHandle = nullptr;

public:
	Resource(int resourceId, const std::string& resourceClass);
	~Resource();

	std::string_view getData();
};
