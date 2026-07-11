#include "bytes.h"

#include <assert.h>

//! Create a mask of `count` bits
int create_mask(uint8_t count)
{
	return ((1 << count) - 1);
}

uint8_t copy_bits(uint8_t byte, uint8_t position, uint8_t count, uint8_t bits)
{
	assert(position + count <= 8);

	const int readMask = create_mask(count);
	const int writeMask = ~(readMask << position);

	return (uint8_t)(
		(byte & writeMask) | ((bits & readMask) << position)
	);
}

void copy_bits_in_place(uint8_t* byte, uint8_t position, uint8_t count, uint8_t bits)
{
	*byte = copy_bits(*byte, position, count, bits);
}

uint8_t get_bits(uint8_t byte, uint8_t position, uint8_t count)
{
	return (uint8_t)(byte & (create_mask(count) << position));
}

bool is_valid_name_char(char c)
{
	return (c >= '0' && c <= '9') ||
		   (c >= 'A' && c <= 'Z') ||
		   (c == 'x') ||
		   (c == ' ');
}

bool sanitize_name_char(char* c)
{
	if (is_valid_name_char(*c))
		return true;

	if (*c >= 'a' && *c <= 'z')
	{
		*c -= 32;
		return true;
	}

	return false;
}

bool sanitize_name(char* name, size_t size)
{
	for (size_t i = 0; i < size; i += 1)
	{
		char* c = name + i;
		if (*c == '\0')
			break;

		if (!sanitize_name_char(c))
			return false;
	}

	return true;
}