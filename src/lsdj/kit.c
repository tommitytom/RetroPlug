#include "kit.h"
#include "sample.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SAMPLE_NAME_OFFSET (0x22)
#define SAMPLE_NAME_SIZE (3)

struct lsdj_kit_t
{
	char name[KIT_NAME_SIZE];
	lsdj_sample_t* samples[KIT_SAMPLE_COUNT];
};

lsdj_kit_t* lsdj_kit_read(lsdj_vio_t* vio, lsdj_error_t** error) {
	int pos = vio->tell(vio->user_data);

	char bank_id[2];
	vio->read(bank_id, 2, vio->user_data);

	// Check to see if this bank is a kit
	if (!bank_is_kit(bank_id) && !bank_is_empty_kit(bank_id)) {
		return NULL;
	}

	lsdj_kit_t* kit = malloc(sizeof(lsdj_kit_t));
	if (kit == NULL) {
		return NULL;
	}

	memset((void*)kit, 0, sizeof(lsdj_kit_t));

	if (bank_is_empty_kit(bank_id)) {
		return kit;
	}

	vio->seek(pos + KIT_NAME_OFFSET, SEEK_SET, vio->user_data);
	vio->read(kit->name, KIT_NAME_SIZE, vio->user_data);

	for (size_t i = 0; i < KIT_SAMPLE_COUNT; ++i) {
		lsdj_resource_offset_t offset = { pos + SAMPLE_NAME_OFFSET, pos };
		kit->samples[i] = lsdj_sample_read(vio, offset, i, error);
	}

	return kit;
}

void lsdj_kit_write(const lsdj_kit_t* kit, lsdj_vio_t* vio, lsdj_error_t** error) {
	int pos = vio->tell(vio->user_data);

	char bank_id[2] = { 0x60, 0x40 };
	vio->write(bank_id, 2, vio->user_data);

	vio->seek(pos + KIT_NAME_OFFSET, SEEK_SET, vio->user_data);
	vio->write(kit->name, KIT_NAME_SIZE, vio->user_data);

	for (size_t i = 0; i < KIT_SAMPLE_COUNT; ++i) {
		lsdj_resource_offset_t offset = { pos + SAMPLE_NAME_OFFSET, pos };
		if (kit->samples[i]) {
			lsdj_sample_write(kit->samples[i], vio, offset, i, error);
		} else {

		}
	}
}

lsdj_sample_t* lsdj_kit_get_sample(lsdj_kit_t* kit, size_t idx) {
	return kit->samples[idx];
}

const char* lsdj_kit_get_name(lsdj_kit_t* kit) {
	return kit->name;
}

void lsdj_kit_free(lsdj_kit_t* kit) {
	if (kit) {
		for (size_t i = 0; i < KIT_SAMPLE_COUNT; ++i) {
			if (kit->samples[i]) {
				lsdj_sample_free(kit->samples[i]);
			}
		}

		free(kit);
	}
}
