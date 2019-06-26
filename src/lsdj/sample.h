#ifndef LSDJ_SAMPLE_H
#define LSDJ_SAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

#include "liblsdj/error.h"
#include "liblsdj/vio.h"

#define KIT_BANK_SIZE (0x4000)
#define SAMPLE_NAME_SIZE (3)

typedef struct {
	int name;
	int data;
} lsdj_resource_offset_t;

typedef struct
{
	char name[SAMPLE_NAME_SIZE];
	unsigned char* data;
} lsdj_sample_t;

lsdj_sample_t* lsdj_sample_read(lsdj_vio_t* vio, lsdj_resource_offset_t offset, size_t idx, lsdj_error_t** error);
void lsdj_sample_free(lsdj_sample_t* sample);

#ifdef __cplusplus
}
#endif

#endif
