#include "file.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>

std::vector<std::uint8_t> readFileContents(std::string_view path)
{
	std::ifstream stream(path.data(), std::ios::binary);
    if (!stream.is_open())
        throw std::runtime_error("Could not open file for reading");
    
	stream.unsetf(std::ios::skipws);

    // Get the size
    stream.seekg(0, std::ios::end);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<unsigned long>(stream.tellg()));
    stream.seekg(0, std::ios::beg);

    bytes.insert(bytes.begin(),
    			 std::istream_iterator<std::uint8_t>(stream),
				 std::istream_iterator<std::uint8_t>());

    return bytes;
}
