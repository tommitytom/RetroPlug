#include "sample.h"

#include <stdlib.h>
#include <string.h>

#define KIT_SAMPLE_COUNT (15)
#define KIT_NAME_OFFSET (0x52)
#define SAMPLE_NAME_OFFSET (0x22)
#define KIT_NAME_SIZE (6)

lsdj_sample_t* lsdj_sample_read(lsdj_vio_t* vio, lsdj_resource_offset_t offset, size_t idx, lsdj_error_t** error) {
	char sample_name[SAMPLE_NAME_SIZE];
	vio->seek(offset.name + (idx * SAMPLE_NAME_SIZE), SEEK_SET, vio->user_data);
	vio->read(sample_name, SAMPLE_NAME_SIZE, vio->user_data);

	if (sample_name[0] == 0) {
		return NULL;
	}

	char header[4];
	vio->seek(offset.data, SEEK_SET, vio->user_data);
	vio->read(header, 4, vio->user_data);
	
	int start = (0xFF & header[0]) | ((0xFF & header[1]) << 8);
	int stop = (0xFF & header[2]) | ((0xFF & header[3]) << 8);

	if (stop <= start) {
		return NULL;
	}

	int sample_data_size = stop - start;

	lsdj_sample_t* sample = malloc(sizeof(lsdj_sample_t));
	memset(sample, 0, sizeof(lsdj_sample_t));
	sample->data = malloc(sample_data_size);

	vio->seek(offset.data + (start - KIT_BANK_SIZE), SEEK_SET, vio->user_data);
	vio->read(sample->data, sample_data_size, vio->user_data);

	memcpy(sample->name, sample_name, SAMPLE_NAME_SIZE);

	return sample;
}

void lsdj_sample_free(lsdj_sample_t* sample) {
	if (sample) {
		if (sample->data) {
			free(sample->data);
		}

		free(sample);
	}
}
