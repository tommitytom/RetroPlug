#ifndef LSDJ_KIT_H
#define LSDJ_KIT_H

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

#include "liblsdj/error.h"
#include "liblsdj/vio.h"
#include "sample.h"

#define KIT_SAMPLE_COUNT (15)
#define KIT_NAME_OFFSET (0x52)
#define KIT_NAME_SIZE (6)

typedef struct lsdj_kit_t lsdj_kit_t;

static int bank_is_kit(const char* id) {
	return id[0] == 0x60 && id[1] == 0x40;
}

static int bank_is_empty_kit(const char* id) {
	return id[0] == -1 && id[1] == -1;
}

lsdj_kit_t* lsdj_kit_read(lsdj_vio_t* vio, lsdj_error_t** error);

void lsdj_kit_write(const lsdj_kit_t* kit, lsdj_vio_t* vio, lsdj_error_t** error);

lsdj_sample_t* lsdj_kit_get_sample(lsdj_kit_t* kit, size_t idx);
const char* lsdj_kit_get_name(lsdj_kit_t* kit);

void lsdj_kit_free(lsdj_kit_t* kit);

#ifdef __cplusplus
}
#endif

#endif
